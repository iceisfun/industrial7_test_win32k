/* Raw input manager (NtRIM*) syscalls that the other areas do not reach: win32kbase!NtRIM* bodies.
 *
 * Gate facts used here (win32kbase analysis):
 *  - NtRIMUnregisterForInput / OnPnpNotification / OnTimerNotification / EnableMonitorMappingForDevice /
 *    SetTestModeStatus first call ApiSetEditionIsUsermodeRIMAccessAllowed(); when it is FALSE they return
 *    STATUS_ACCESS_DENIED (0xC0000022) without touching the arguments. When it is TRUE the handle is
 *    resolved through the RawInputManager object type and a bogus handle yields a failure NTSTATUS.
 *    Which of the two a given edition produces is an SKU fact, so the tests only demand "negative
 *    NTSTATUS" and note the exact value.
 *  - NtRIMFreeInputBuffer always returns 0 (the gate result is discarded).
 *  - NtRIMRemoveInputObserver is ObCloseHandle(h, UserMode): it closes any handle.
 *  - NtRIMSetExtendedDeviceProperty rejects (NULL buffer || cb != 0x14) with STATUS_INVALID_PARAMETER first.
 *  - NtRIMQueryDevicePath takes a UNICODE_STRING that must start with "\\?" (3 chars) and returns
 *    STATUS_NOT_FOUND for a path that matches no RIM device.
 */
#include "r2ip.h"
#include <stdlib.h>

static HANDLE mk_event(void) { return CreateEventW(NULL, TRUE, FALSE, NULL); }

/* ---- NtRIMQueryDevicePath ------------------------------------------------------------------ */

static LONG query_path(const wchar_t *s, HANDLE *out) {
    UNICODE_STRING us;
    us.Length = (USHORT)(wcslen(s) * 2);
    us.MaximumLength = us.Length;
    us.Buffer = (PWSTR)s;
    return UNW(NtRIMQueryDevicePath)(&us, out);
}

/* The first three wide chars must be '\\','\\','?': NtRIMQueryDevicePath compares those three and returns STATUS_INVALID_PARAMETER
 * otherwise. Every case here has at least three characters: the kernel reads three characters whatever the string length, so for a
 * shorter string the third one is stack garbage and the answer varied between runs (NOT_FOUND once the garbage was a '?'). */
static const wchar_t *const bad_prefix[] = {
    L"C:\\Windows", L"\\\\.\\HID#VID_0000", L"\\??\\HID#VID_0000", L"\\\\.", L"\\ab", L"abc", L"\\\\x\\y", L"?\\\\HID", L"HID#VID_1234&PID_5678",
};
RT(rim_querydevicepath_bad_prefix_is_invalid_parameter) {
    REQUIRE(UNW(NtRIMQueryDevicePath));
    for (size_t i = 0; i < sizeof bad_prefix / sizeof *bad_prefix; i++) {
        HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
        LONG st = query_path(bad_prefix[i], &out);
        CHECKF(st == R2_ST_INVALID_PARAMETER, "path %u st=0x%08lx", (unsigned)i, (unsigned long)st);
        EXPECT_EQ(out, 0x5a5a5a5a);            /* the output is only written on success */
    }
}

RT(rim_querydevicepath_unknown_device_is_not_found) {
    REQUIRE(UNW(NtRIMQueryDevicePath));
    wchar_t p[160];
    _snwprintf(p, 160, L"\\\\?\\HID#win32kt_nonexistent_%lu#7&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}", GetCurrentProcessId());
    HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
    LONG st = query_path(p, &out);
    EXPECT_EQ(st, R2_ST_NOT_FOUND);             /* the walk over the registered devices finds nothing: the status stays NOT_FOUND */
    EXPECT_EQ(out, 0x5a5a5a5a);
}

RT(rim_querydevicepath_prefix_only_is_not_found) {
    REQUIRE(UNW(NtRIMQueryDevicePath));
    HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
    LONG st = query_path(L"\\\\?", &out);       /* exactly the three prefix chars: valid prefix, no such device */
    EXPECT_EQ(st, R2_ST_NOT_FOUND);
    EXPECT_EQ(out, 0x5a5a5a5a);
}

RT(rim_querydevicepath_case_sensitive_prefix) {
    REQUIRE(UNW(NtRIMQueryDevicePath));
    HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
    LONG st = query_path(L"\\\\?\\hid#win32kt_none", &out);
    EXPECT_EQ(st, R2_ST_NOT_FOUND);
}

/* Opens each real raw input device by its RIDI_DEVICENAME path (the RIM keeps the same "\\?\..." name,
 * rewritten to "\??\..." for the comparison). The result depends on the machine: success returns a
 * process handle that must be closed; STATUS_NOT_FOUND is noted. */
RT(rim_querydevicepath_real_devices) {
    REQUIRE(UNW(NtRIMQueryDevicePath) && U(GetRawInputDeviceList) && U(GetRawInputDeviceInfoW));
    UINT n = 0;
    if (U(GetRawInputDeviceList)(NULL, &n, sizeof(RAWINPUTDEVICELIST)) != 0 || n == 0 || n > 256) { t_skip("no raw input devices (n=%u)", n); return; }
    RAWINPUTDEVICELIST *l = calloc(n + 4, sizeof *l);
    UINT got = U(GetRawInputDeviceList)(l, &n, sizeof *l);
    int opened = 0, notfound = 0;
    for (UINT i = 0; i < got && i < 32; i++) {
        wchar_t name[512]; UINT cch = 512;
        if ((int)U(GetRawInputDeviceInfoW)(l[i].hDevice, RIDI_DEVICENAME, name, &cch) <= 0) continue;
        HANDLE h = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
        LONG st = query_path(name, &h);
        if (st == 0) {
            opened++;
            CHECK(h != NULL && h != (HANDLE)(ULONG_PTR)0x5a5a5a5a);
            DWORD fl = 0;
            CHECK(GetHandleInformation(h, &fl));      /* a real, process-visible handle */
            CloseHandle(h);
            SET_ERR(0);
            CHECK(!GetHandleInformation(h, &fl));     /* and closing it works like any handle */
        } else {
            notfound++;
            CHECKF(st == R2_ST_NOT_FOUND || st < 0, "st=0x%08lx", (unsigned long)st);
            EXPECT_EQ(h, 0x5a5a5a5a);
        }
    }
    t_note("RIM device paths: %d opened, %d not opened (of %u devices)", opened, notfound, got);
    free(l);
}

/* For a HID device reached through QueryDevicePath, the lockfree preparsed-data query with a NULL buffer
 * reports the size (a NULL buffer makes the call report the size through the length pointer). */
RT(rim_preparsed_size_for_real_hid_device) {
    REQUIRE(UNW(NtRIMQueryDevicePath) && UNW(NtRIMGetDevicePreparsedDataLockfree) && U(GetRawInputDeviceList) && U(GetRawInputDeviceInfoW));
    UINT n = 0;
    if (U(GetRawInputDeviceList)(NULL, &n, sizeof(RAWINPUTDEVICELIST)) != 0 || n == 0 || n > 256) { t_skip("no raw input devices"); return; }
    RAWINPUTDEVICELIST *l = calloc(n + 4, sizeof *l);
    UINT got = U(GetRawInputDeviceList)(l, &n, sizeof *l);
    int tested = 0;
    for (UINT i = 0; i < got && i < 32 && tested < 4; i++) {
        if (l[i].dwType != RIM_TYPEHID) continue;
        wchar_t name[512]; UINT cch = 512;
        if ((int)U(GetRawInputDeviceInfoW)(l[i].hDevice, RIDI_DEVICENAME, name, &cch) <= 0) continue;
        HANDLE h = NULL;
        if (query_path(name, &h) != 0 || !h) continue;
        UINT want = 0;
        U(GetRawInputDeviceInfoW)(l[i].hDevice, RIDI_PREPARSEDDATA, NULL, &want);
        UINT sz = 0xdeadbeef;
        LONG st = UNW(NtRIMGetDevicePreparsedDataLockfree)(h, NULL, &sz);
        if (st < 0) t_note("preparsed size query on a QueryDevicePath handle -> 0x%08lx", (unsigned long)st);   /* the handle's access mask may be too weak */
        if (st >= 0) {
            CHECK(sz != 0xdeadbeef);
            if (want) CHECKF(sz == want, "kernel size %u user32 size %u", sz, want);
            /* a too-small buffer is truncated, not rejected: memcpy(min(*pcb, size)) */
            if (sz > 4) {
                unsigned char *b = calloc(1, sz + 16);
                memset(b, 0xCC, sz + 16);
                UINT cap = 4;
                st = UNW(NtRIMGetDevicePreparsedDataLockfree)(h, b, &cap);
                CHECKF(st >= 0, "st=0x%08lx", (unsigned long)st);
                EXPECT_EQ(b[4], 0xCC);              /* nothing written past *pcb */
                free(b);
            }
        }
        CloseHandle(h);
        tested++;
    }
    if (!tested) t_note("no HID raw input device reachable through NtRIMQueryDevicePath");
    free(l);
}

/* ---- invalid handle behaviour of the lockfree getters ------------------------------------- */

static void bad_handles(HANDLE *out, int *n, HANDLE ev) {
    int i = 0;
    out[i++] = NULL;
    out[i++] = (HANDLE)(ULONG_PTR)0x7ffc;       /* never-allocated slot */
    out[i++] = ev;                              /* a real handle of the wrong object type */
    *n = i;
}

RT(rim_preparsed_lockfree_bad_handle_fails) {
    REQUIRE(UNW(NtRIMGetDevicePreparsedDataLockfree));
    HANDLE ev = mk_event(); HANDLE hs[4]; int n;
    bad_handles(hs, &n, ev);
    for (int i = 0; i < n; i++) {
        UINT sz = 0x1234;
        LONG st = UNW(NtRIMGetDevicePreparsedDataLockfree)(hs[i], NULL, &sz);
        CHECKF(st < 0, "case %d st=0x%08lx", i, (unsigned long)st);   /* RawInputManagerDeviceObjectResolveHandle fails before *pcb is touched */
        EXPECT_EQ(sz, 0x1234);
        t_note("preparsed lockfree bad handle %d -> 0x%08lx", i, (unsigned long)st);
    }
    CloseHandle(ev);
}

RT(rim_properties_lockfree_bad_handle_fails) {
    REQUIRE(UNW(NtRIMGetDevicePropertiesLockfree));
    HANDLE ev = mk_event(); HANDLE hs[4]; int n;
    bad_handles(hs, &n, ev);
    for (int i = 0; i < n; i++) {
        LONG st = UNW(NtRIMGetDevicePropertiesLockfree)(hs[i], NULL);   /* the props buffer is only read after the handle resolved */
        CHECKF(st < 0, "case %d st=0x%08lx", i, (unsigned long)st);
        t_note("properties lockfree bad handle %d -> 0x%08lx", i, (unsigned long)st);
    }
    CloseHandle(ev);
}

/* A handle that was valid and then closed must stop resolving. */
RT(rim_lockfree_closed_handle_fails) {
    REQUIRE(UNW(NtRIMGetDevicePropertiesLockfree) && UNW(NtRIMGetDevicePreparsedDataLockfree));
    HANDLE ev = mk_event();
    CloseHandle(ev);
    UINT sz = 0;
    CHECK(UNW(NtRIMGetDevicePropertiesLockfree)(ev, NULL) < 0);
    CHECK(UNW(NtRIMGetDevicePreparsedDataLockfree)(ev, NULL, &sz) < 0);
}

/* ---- NtRIMSetExtendedDeviceProperty -------------------------------------------------------- */

RT(rim_setextended_rejects_null_buffer) {
    REQUIRE(UNW(NtRIMSetExtendedDeviceProperty));
    EXPECT_EQ(UNW(NtRIMSetExtendedDeviceProperty)(NULL, NULL, 0x14), R2_ST_INVALID_PARAMETER);
    EXPECT_EQ(UNW(NtRIMSetExtendedDeviceProperty)((HANDLE)(ULONG_PTR)4, NULL, 0x14), R2_ST_INVALID_PARAMETER);
}

RT(rim_setextended_rejects_wrong_size) {
    REQUIRE(UNW(NtRIMSetExtendedDeviceProperty));
    static const int sizes[] = { 0, 1, 0x10, 0x13, 0x15, 0x18, 0x28, 0x100, -1, 0x14000 };
    DWORD prop[8] = { 0 };
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        LONG st = UNW(NtRIMSetExtendedDeviceProperty)(NULL, prop, sizes[i]);
        CHECKF(st == R2_ST_INVALID_PARAMETER, "cb=%d st=0x%08lx", sizes[i], (unsigned long)st);   /* cb != 0x14 */
    }
}

RT(rim_setextended_bad_handle_fails_after_size_check) {
    REQUIRE(UNW(NtRIMSetExtendedDeviceProperty));
    DWORD prop[5] = { 0 };
    HANDLE ev = mk_event(); HANDLE hs[4]; int n;
    bad_handles(hs, &n, ev);
    for (int i = 0; i < n; i++) {
        LONG st = UNW(NtRIMSetExtendedDeviceProperty)(hs[i], prop, 0x14);
        CHECKF(st < 0, "case %d st=0x%08lx", i, (unsigned long)st);   /* RawInputManagerDeviceObjectResolveHandle fails */
        t_note("SetExtendedDeviceProperty bad handle %d -> 0x%08lx", i, (unsigned long)st);
    }
    CloseHandle(ev);
}

/* ---- NtRIMFreeInputBuffer: returns 0 whatever happens -------------------------------------- */

RT(rim_freeinputbuffer_always_returns_zero) {
    REQUIRE(UNW(NtRIMFreeInputBuffer));
    HANDLE ev = mk_event();
    EXPECT_EQ(UNW(NtRIMFreeInputBuffer)(NULL, NULL), 0);
    EXPECT_EQ(UNW(NtRIMFreeInputBuffer)(ev, NULL), 0);
    EXPECT_EQ(UNW(NtRIMFreeInputBuffer)((HANDLE)(ULONG_PTR)0x7ffc, NULL), 0);
    CloseHandle(ev);
    EXPECT_EQ(UNW(NtRIMFreeInputBuffer)(ev, NULL), 0);
}

/* ---- NtRIMRemoveInputObserver == ObCloseHandle(h, UserMode) --------------------------------- */

RT(rim_removeinputobserver_closes_event_handle) {
    REQUIRE(UNW(NtRIMRemoveInputObserver));
    HANDLE ev = mk_event();
    DWORD fl = 0;
    CHECK(GetHandleInformation(ev, &fl));
    LONG st = UNW(NtRIMRemoveInputObserver)(ev);
    EXPECT_EQ(st, 0);
    SET_ERR(0);
    CHECK(!GetHandleInformation(ev, &fl));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

RT(rim_removeinputobserver_closes_mutex_and_semaphore) {
    REQUIRE(UNW(NtRIMRemoveInputObserver));
    HANDLE h[2];
    h[0] = CreateMutexW(NULL, FALSE, NULL);
    h[1] = CreateSemaphoreW(NULL, 0, 1, NULL);
    for (int i = 0; i < 2; i++) {
        EXPECT_EQ(UNW(NtRIMRemoveInputObserver)(h[i]), 0);          /* no object-type check in the thunk */
        DWORD fl;
        SET_ERR(0);
        CHECK(!GetHandleInformation(h[i], &fl));
    }
}

RT(rim_removeinputobserver_honors_noclose_protection) {
    REQUIRE(UNW(NtRIMRemoveInputObserver));
    HANDLE ev = mk_event();
    DWORD fl = 0;
    CHECK(SetHandleInformation(ev, HANDLE_FLAG_PROTECT_FROM_CLOSE, HANDLE_FLAG_PROTECT_FROM_CLOSE));
    /* ObCloseHandle(UserMode) on a protected handle fails (STATUS_HANDLE_NOT_CLOSABLE, 0xC0000235) and
     * must not raise: a debugger is not attached. The handle stays valid. */
    LONG st = UNW(NtRIMRemoveInputObserver)(ev);
    t_note("RemoveInputObserver on protected handle -> 0x%08lx", (unsigned long)st);
    CHECK(st < 0);
    CHECK(GetHandleInformation(ev, &fl));
    CHECK(SetHandleInformation(ev, HANDLE_FLAG_PROTECT_FROM_CLOSE, 0));
    CloseHandle(ev);
}

RT(rim_observenextinput_bad_handle_fails) {
    REQUIRE(UNW(NtRIMObserveNextInput));
    HANDLE ev = mk_event(); HANDLE hs[4]; int n;
    bad_handles(hs, &n, ev);
    for (int i = 0; i < n; i++) {
        LONG st = UNW(NtRIMObserveNextInput)(hs[i]);     /* RawInputManagerInputObserverObjectResolveHandle fails */
        CHECKF(st < 0, "case %d st=0x%08lx", i, (unsigned long)st);
        t_note("ObserveNextInput bad handle %d -> 0x%08lx", i, (unsigned long)st);
    }
    DWORD fl;
    CHECK(GetHandleInformation(ev, &fl));               /* the failed call must not have closed it */
    CloseHandle(ev);
}

/* ---- edition-gated entry points ------------------------------------------------------------- */

RT(rim_unregister_bad_handle_fails) {
    REQUIRE(UNW(NtRIMUnregisterForInput));
    HANDLE ev = mk_event(); HANDLE hs[4]; int n;
    bad_handles(hs, &n, ev);
    for (int i = 0; i < n; i++) {
        LONG st = UNW(NtRIMUnregisterForInput)(hs[i]);
        CHECKF(st < 0, "case %d st=0x%08lx", i, (unsigned long)st);
        t_note("UnregisterForInput bad handle %d -> 0x%08lx", i, (unsigned long)st);
    }
    CloseHandle(ev);
}

RT(rim_onpnp_bad_handle_fails) {
    REQUIRE(UNW(NtRIMOnPnpNotification));
    HANDLE ev = mk_event(); HANDLE hs[4]; int n;
    bad_handles(hs, &n, ev);
    for (int i = 0; i < n; i++) {
        LONG st = UNW(NtRIMOnPnpNotification)(hs[i]);
        CHECKF(st < 0, "case %d st=0x%08lx", i, (unsigned long)st);
        t_note("OnPnpNotification bad handle %d -> 0x%08lx", i, (unsigned long)st);
    }
    CloseHandle(ev);
}

RT(rim_ontimer_bad_handle_fails) {
    REQUIRE(UNW(NtRIMOnTimerNotification));
    HANDLE ev = mk_event(); HANDLE hs[4]; int n;
    bad_handles(hs, &n, ev);
    for (int i = 0; i < n; i++) {
        LONG st = UNW(NtRIMOnTimerNotification)(hs[i], 0);
        CHECKF(st < 0, "case %d st=0x%08lx", i, (unsigned long)st);
        t_note("OnTimerNotification bad handle %d -> 0x%08lx", i, (unsigned long)st);
    }
    CloseHandle(ev);
}

RT(rim_enablemonitormapping_bad_handle_fails) {
    REQUIRE(UNW(NtRIMEnableMonitorMappingForDevice));
    HANDLE ev = mk_event(); HANDLE hs[4]; int n;
    bad_handles(hs, &n, ev);
    for (int i = 0; i < n; i++) {
        LONG st = UNW(NtRIMEnableMonitorMappingForDevice)(hs[i], NULL, NULL);
        CHECKF(st < 0, "case %d st=0x%08lx", i, (unsigned long)st);
        t_note("EnableMonitorMappingForDevice bad handle %d -> 0x%08lx", i, (unsigned long)st);
    }
    CloseHandle(ev);
}

/* RIMSetTestModeStatus(0) only clears gbTestModeOn (default 0) and walks the list: with the gate closed the
 * call is STATUS_ACCESS_DENIED, with it open STATUS_SUCCESS. Never switched on. */
RT(rim_settestmodestatus_off_is_denied_or_success) {
    REQUIRE(UNW(NtRIMSetTestModeStatus));
    LONG st = UNW(NtRIMSetTestModeStatus)(0);
    CHECKF(st == R2_ST_ACCESS_DENIED || st == 0, "st=0x%08lx", (unsigned long)st);
    t_note("NtRIMSetTestModeStatus(0) -> 0x%08lx", (unsigned long)st);
}

/* The gate is process wide: all five gated entry points agree about it. */
RT(rim_gate_is_consistent_across_entry_points) {
    REQUIRE(UNW(NtRIMUnregisterForInput) && UNW(NtRIMOnPnpNotification) && UNW(NtRIMOnTimerNotification) && UNW(NtRIMEnableMonitorMappingForDevice) && UNW(NtRIMSetTestModeStatus));
    LONG s[5];
    s[0] = UNW(NtRIMUnregisterForInput)(NULL);
    s[1] = UNW(NtRIMOnPnpNotification)(NULL);
    s[2] = UNW(NtRIMOnTimerNotification)(NULL, 0);
    s[3] = UNW(NtRIMEnableMonitorMappingForDevice)(NULL, NULL, NULL);
    s[4] = UNW(NtRIMSetTestModeStatus)(0);
    int denied = 0;
    for (int i = 0; i < 4; i++) denied += (s[i] == R2_ST_ACCESS_DENIED);
    CHECKF(denied == 0 || denied == 4, "denied=%d (%08lx %08lx %08lx %08lx)", denied, (unsigned long)s[0], (unsigned long)s[1], (unsigned long)s[2], (unsigned long)s[3]);
    if (denied == 4) EXPECT_EQ(s[4], R2_ST_ACCESS_DENIED);
    else EXPECT_EQ(s[4], 0);
}

/* NULL handle with the gate closed vs open: closed gives exactly ACCESS_DENIED for every gated call. */
RT(rim_gate_closed_means_access_denied_exactly) {
    REQUIRE(UNW(NtRIMUnregisterForInput) && UNW(NtRIMSetTestModeStatus));
    LONG s = UNW(NtRIMSetTestModeStatus)(0);
    LONG u = UNW(NtRIMUnregisterForInput)(NULL);
    if (s == R2_ST_ACCESS_DENIED) EXPECT_EQ(u, R2_ST_ACCESS_DENIED);
    else { t_note("RIM usermode access is allowed here (unregister -> 0x%08lx)", (unsigned long)u); CHECK(u < 0); }
}

/* ---- stub shape of this area's syscalls ---------------------------------------------------- */

struct stubrow { const char *name; long number; };
static const struct stubrow stubs[] = {
    { "NtRIMQueryDevicePath", 4991 }, { "NtRIMGetDevicePreparsedDataLockfree", 4983 },
    { "NtRIMGetDevicePropertiesLockfree", 4985 }, { "NtRIMSetExtendedDeviceProperty", 4995 },
    { "NtRIMFreeInputBuffer", 4981 }, { "NtRIMObserveNextInput", 4988 }, { "NtRIMRemoveInputObserver", 4994 },
    { "NtRIMUnregisterForInput", 4997 }, { "NtRIMOnPnpNotification", 4989 }, { "NtRIMOnTimerNotification", 4990 },
    { "NtRIMSetTestModeStatus", 4996 }, { "NtRIMEnableMonitorMappingForDevice", 4980 },
    { "NtUserInjectKeyboardInput", 5218 }, { "NtUserInjectMouseInput", 5219 }, { "NtUserInjectDeviceInput", 5215 },
    { "NtUserInjectGenericHidInput", 5216 }, { "NtUserInitializeGenericHidInjection", 5210 },
    { "NtUserInitializeInputDeviceInjection", 5211 }, { "NtUserInitializePointerDeviceInjection", 5212 },
    { "NtUserInitializePointerDeviceInjectionEx", 5213 }, { "NtUserRemoveInjectionDevice", 5299 },
    { "NtUserClearForeground", 5042 }, { "NtUserSetFallbackForeground", 5326 }, { "NtUserSetBrokeredForeground", 5312 },
    { "NtUserCanBrokerForceForeground", 5034 }, { "NtUserSetForegroundWindowForApplication", 5328 },
    { "NtUserNavigateFocus", 5255 }, { "NtUserLockCursor", 5237 }, { "NtUserShowSystemCursor", 5373 },
    { "NtUserGetCursorFrameInfo", 4325 }, { "NtUserInternalGetWindowIcon", 5223 }, { "NtUserIsMouseInputEnabled", 5227 },
    { "NtUserSetThreadInputBlocked", 5360 }, { "NtUserMarkWindowForRawMouse", 5249 },
    { "NtUserPostKeyboardInputMessage", 5263 }, { "NtUserPromoteMouseInPointer", 5266 },
    { "NtUserGetQueueStatus", 5170 }, { "NtUserGetQueueStatusReadonly", 5171 }, { "NtUserWaitAvailableMessageEx", 5402 },
    { "NtUserWaitForMsgAndEvent", 5404 }, { "NtUserRealWaitMessageEx", 5275 }, { "NtUserRealInternalGetMessage", 4317 },
};

/* tools/win32u_syscalls.tsv lists the service number (decimal, 0x1000 + table index) that the stub's
 * "mov eax, imm32" carries. */
RT(rim_stub_numbers_match_table) {
    if (t_is_wine()) { t_skip("emulated win32u has no syscall stubs"); return; }
    for (size_t i = 0; i < sizeof stubs / sizeof *stubs; i++) {
        FARPROC p = t_proc_in("win32u.dll", stubs[i].name);
        if (!p) { t_note("%s not exported here", stubs[i].name); continue; }
        CHECKF(k_stub_valid(p), "%s stub prologue", stubs[i].name);
        long n = k_stub_number(p);
        CHECKF(n == stubs[i].number, "%s number %ld want %ld", stubs[i].name, n, stubs[i].number);
    }
}
