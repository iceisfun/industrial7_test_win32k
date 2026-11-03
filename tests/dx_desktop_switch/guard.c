/* dx_desktop_switch: shared helpers, the restore guard (watchdog child) and the tests of the guard
 * itself. Nothing in this area ever logs off, locks, or shuts down the session. */
#include "dxd.h"
#include <stdlib.h>

int dxd_cf;

/* ================================ small utilities ================================================ */

void dxd_name(WCHAR *out, const char *tag) { swprintf(out, 64, L"win32kt%lu_%S", GetCurrentProcessId(), tag); }

BOOL dxd_uoi_str(HANDLE h, int idx, WCHAR *out, DWORD cch) {
    DWORD need = 0; out[0] = 0;
    return U(GetUserObjectInformationW)(h, idx, out, cch * sizeof(WCHAR), &need);
}

static p_NtQueryObject qo(void) { return (p_NtQueryObject)t_proc_in("ntdll.dll", "NtQueryObject"); }

BOOL dxd_obj_type(HANDLE h, WCHAR *out, size_t cch) {
    p_NtQueryObject q = qo(); BYTE buf[512]; ULONG rl = 0;
    out[0] = 0;
    if (!q || q(h, 2 /* ObjectTypeInformation */, buf, sizeof buf, &rl) < 0) return FALSE;
    UNICODE_STRING *us = (UNICODE_STRING *)buf;
    size_t n = us->Length / sizeof(WCHAR);
    if (n >= cch) n = cch - 1;
    memcpy(out, us->Buffer, n * sizeof(WCHAR)); out[n] = 0;
    return TRUE;
}

BOOL dxd_granted(HANDLE h, ULONG *granted) {
    p_NtQueryObject q = qo(); ULONG b[16] = {0}, rl = 0;   /* PUBLIC_OBJECT_BASIC_INFORMATION: GrantedAccess at +4 */
    if (!q || q(h, 0 /* ObjectBasicInformation */, b, sizeof b, &rl) < 0) return FALSE;
    *granted = b[1];
    return TRUE;
}

HDESK dxd_mk_desk(const char *tag, DWORD access, WCHAR *name_out) {
    WCHAR n[64]; dxd_name(n, tag);
    if (name_out) lstrcpyW(name_out, n);
    return U(CreateDesktopW)(n, NULL, NULL, 0, access, NULL);
}

BOOL dxd_same_object(HANDLE a, HANDLE b) {
    p_CompareObjectHandles cmp = (p_CompareObjectHandles)t_proc_in("kernelbase.dll", "CompareObjectHandles");
    if (cmp) return cmp(a, b);
    WCHAR na[128], nb[128];
    return dxd_uoi_str(a, DXD_UOI_NAME, na, 128) && dxd_uoi_str(b, DXD_UOI_NAME, nb, 128) && !lstrcmpiW(na, nb);
}

BOOL dxd_winsta_name(WCHAR *out, size_t cch) {
    HWINSTA ws = U(GetProcessWindowStation)();
    out[0] = 0;
    return ws && dxd_uoi_str(ws, DXD_UOI_NAME, out, (DWORD)cch);
}

/* ================================ guard ===========================================================
 * The original input desktop is recorded once. The watchdog child polls the input desktop every
 * 250 ms; if the input desktop is not the original one for more than 5 s, or the monitored process
 * has exited while it is not the original, it switches back (retrying for a minute). */

static HDESK g_orig;
static WCHAR g_orig_name[128];
static int g_guard_state;            /* 0 not tried, 1 up, -1 unavailable */
static HANDLE g_wd;

BOOL dxd_input_name(WCHAR *out, size_t cch) {
    out[0] = 0;
    if (!U(OpenInputDesktop)) return FALSE;
    HDESK h = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
    if (!h) return FALSE;
    BOOL ok = dxd_uoi_str(h, DXD_UOI_NAME, out, (DWORD)cch);
    U(CloseDesktop)(h);
    return ok;
}
const WCHAR *dxd_orig_name(void) { return g_orig_name; }
HDESK dxd_orig_handle(void) { return g_orig; }

static void guard_atexit(void) {
    if (g_guard_state == 1) dxd_restore();
}

int dxd_restore(void) {
    if (g_guard_state != 1) return 1;
    DWORD t0 = GetTickCount(); int fails = 0;
    for (;;) {
        WCHAR cur[128];
        if (dxd_input_name(cur, 128) && !lstrcmpiW(cur, g_orig_name)) return 1;
        if (!U(SwitchDesktop)(g_orig)) {
            DWORD e = GetLastError();
            if (++fails == 3)
                t_note("LOUD: cannot switch back to the original input desktop \"%ls\" after 3 tries (err %lu); still retrying",
                       g_orig_name, e);
        }
        if (GetTickCount() - t0 > 60000) {
            t_note("LOUD: giving up restoring the input desktop \"%ls\" after 60s; the watchdog child keeps trying", g_orig_name);
            return 0;
        }
        Sleep(200);
    }
}

int dxd_guard_begin(void) {
    if (t_is_wine()) return 0;
    if (g_guard_state) return g_guard_state > 0;
    g_guard_state = -1;
    if (!U(OpenInputDesktop) || !U(SwitchDesktop) || !U(CloseDesktop) || !U(GetUserObjectInformationW)) return 0;
    g_orig = U(OpenInputDesktop)(0, FALSE, DESKTOP_SWITCHDESKTOP | DESKTOP_READOBJECTS);
    if (!g_orig || !dxd_uoi_str(g_orig, DXD_UOI_NAME, g_orig_name, 128)) return 0;
    char nm[256], extra[320];
    WideCharToMultiByte(CP_ACP, 0, g_orig_name, -1, nm, sizeof nm, NULL, NULL);
    snprintf(extra, sizeof extra, "%lu %s", GetCurrentProcessId(), nm);
    g_wd = t_spawn_child("dxd_watchdog", extra, CREATE_NO_WINDOW, NULL, NULL, NULL, FALSE, NULL, NULL);
    atexit(guard_atexit);
    g_guard_state = 1;
    return 1;
}

void dxd_expect_unchanged(void) {
    WCHAR cur[128];
    BOOL ok = dxd_input_name(cur, 128);
    CHECK(ok);
    if (ok && lstrcmpiW(cur, g_orig_name)) {
        CHECKF(0, "input desktop is \"%ls\", want \"%ls\"", cur, g_orig_name);
        dxd_restore();                       /* an attempt that must have failed moved the input: put it back */
    }
}

int dxd_begin(void) {
    /* Wine's win32u aborts the process on NtUserSwitchDesktop (unimplemented); nothing to validate there */
    if (t_is_wine()) { t_skip("NtUserSwitchDesktop is unimplemented under Wine"); return 0; }
    if (!U(SwitchDesktop) || !U(OpenInputDesktop) || !U(CreateDesktopW) || !U(CloseDesktop)) { t_skip("missing user32 desktop exports"); return 0; }
    if (!dxd_guard_begin()) { t_skip("no input desktop to guard (not an interactive session?)"); return 0; }
    /* a previous test that crashed mid-switch must not leave us elsewhere */
    if (!dxd_restore()) { t_skip("could not return to the original input desktop"); return 0; }
    return 1;
}

void dxd_do_switch(HDESK d, struct dxd_sw *s, DWORD dwell_ms) {
    memset(s, 0, sizeof *s);
    SetLastError(0);
    s->rc = U(SwitchDesktop)(d);
    s->err = GetLastError();
    if (s->rc && dwell_ms) Sleep(dwell_ms);
    s->in_ok = dxd_input_name(s->in_name, 128);
}

/* ---- the watchdog and the helper children ----------------------------------------------------- */

CHILD(dxd_watchdog) {
    /* argv: dxd_watchdog <monitored pid> <original input desktop name> */
    if (argc < 3) return 32;
    DWORD pid = (DWORD)strtoul(argv[1], NULL, 10);
    WCHAR want[128]; MultiByteToWideChar(CP_ACP, 0, argv[2], -1, want, 128);
    if (!U(OpenDesktopW) || !U(OpenInputDesktop) || !U(SwitchDesktop)) return 0;   /* nothing to guard on this system */
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!parent) return 31;
    HDESK od = U(OpenDesktopW)(want, 0, FALSE, DESKTOP_SWITCHDESKTOP | DESKTOP_READOBJECTS);
    if (!od) { CloseHandle(parent); return 30; }   /* cannot restore to a desktop we cannot open: refuse to act */
    DWORD off_since = 0; int restored = 0, failed = 0;
    for (;;) {
        int dead = WaitForSingleObject(parent, 250) == WAIT_OBJECT_0;
        WCHAR cur[128];
        HDESK ih = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
        BOOL known = FALSE; cur[0] = 0;
        if (ih) { known = dxd_uoi_str(ih, DXD_UOI_NAME, cur, 128); U(CloseDesktop)(ih); }
        if (known && lstrcmpiW(cur, want)) {
            if (!off_since) off_since = GetTickCount();
            if (dead || GetTickCount() - off_since > 5000) {
                int ok = 0;
                for (int i = 0; i < 120 && !ok; i++) {
                    if (U(SwitchDesktop)(od)) ok = 1; else Sleep(500);
                }
                if (ok) restored = 1; else failed = 1;
                off_since = 0;
            }
        } else off_since = 0;
        if (dead) break;
    }
    return failed ? 20 : restored ? 10 : 0;
}

CHILD(dxd_sleeper) { Sleep(60000); return 0; }

CHILD(dxd_switcher) {
    /* creates a desktop, switches the input there and then waits to be killed (the parent plays "crash") */
    WCHAR n[64]; HDESK d;
    dxd_name(n, "swk");
    d = U(CreateDesktopW)(n, NULL, NULL, 0, DESKTOP_ALL_RIGHTS, NULL);
    if (!d) { printf("SWITCHER nodesk %lu\n", GetLastError()); return 0; }
    BOOL rc = U(SwitchDesktop)(d);
    printf("SWITCHER rc=%d err=%lu\n", rc, GetLastError());
    Sleep(30000);
    return 0;
}

static HANDLE spawn_plain(const char *child, const char *extra) {
    return t_spawn_child(child, extra, CREATE_NO_WINDOW, NULL, NULL, NULL, FALSE, NULL, NULL);
}
static DWORD wait_exit(HANDLE h, DWORD ms) {
    DWORD code = 0xDEAD0001;
    if (WaitForSingleObject(h, ms) == WAIT_OBJECT_0) GetExitCodeProcess(h, &code);
    return code;
}

/* ================================ tests of the guard ============================================= */

DXT(guard_records_original_input_desktop) {
    DXD_BEGIN();
    CHECK(dxd_orig_handle() != NULL);
    CHECK(dxd_orig_name()[0] != 0);
    WCHAR cur[128];
    CHECK(dxd_input_name(cur, 128));
    EXPECT_WSTR(cur, dxd_orig_name());      /* nothing has switched yet */
}

DXT(guard_original_handle_has_switch_access) {
    DXD_BEGIN();
    ULONG g = 0;
    /* EditionOpenInputDesktopEntryPoint ORs 0x81 into the requested mask; SWITCHDESKTOP (0x100) was requested. */
    if (!dxd_granted(dxd_orig_handle(), &g)) { t_skip("NtQueryObject unavailable"); return; }
    CHECKF((g & 0x181) == 0x181, "granted 0x%lx", g);
}

DXT(guard_original_handle_is_desktop_type) {
    DXD_BEGIN();
    WCHAR t[32];
    CHECK(dxd_obj_type(dxd_orig_handle(), t, 32));
    EXPECT_WSTR(t, L"Desktop");
    CHECK(dxd_uoi_str(dxd_orig_handle(), DXD_UOI_TYPE, t, 32));
    EXPECT_WSTR(t, L"Desktop");
}

DXT(guard_restore_is_idempotent_when_already_original) {
    DXD_BEGIN();
    for (int i = 0; i < 3; i++) CHECK(dxd_restore());
    WCHAR cur[128]; CHECK(dxd_input_name(cur, 128)); EXPECT_WSTR(cur, dxd_orig_name());
}

DXT(guard_watchdog_exits_quietly_when_nothing_to_restore) {
    DXD_BEGIN();
    HANDLE sl = spawn_plain("dxd_sleeper", "");
    if (!sl) { t_skip("cannot spawn"); return; }
    char nm[256], extra[320]; DWORD spid = GetProcessId(sl);
    WideCharToMultiByte(CP_ACP, 0, dxd_orig_name(), -1, nm, sizeof nm, NULL, NULL);
    snprintf(extra, sizeof extra, "%lu %s", spid, nm);
    HANDLE wd = spawn_plain("dxd_watchdog", extra);
    CHECK(wd != NULL);
    Sleep(600);
    TerminateProcess(sl, 1);                        /* the monitored process dies; input is still the original */
    if (wd) {
        DWORD code = wait_exit(wd, 10000);
        CHECKF(code == 0, "watchdog exit code %lu", code);
        if (code == 0xDEAD0001) TerminateProcess(wd, 1);
        CloseHandle(wd);
    }
    WaitForSingleObject(sl, 5000); CloseHandle(sl);
}

DXT(guard_watchdog_refuses_unknown_original) {
    DXD_BEGIN();
    HANDLE sl = spawn_plain("dxd_sleeper", "");
    if (!sl) { t_skip("cannot spawn"); return; }
    char extra[128];
    snprintf(extra, sizeof extra, "%lu win32kt_no_such_desktop_%lu", GetProcessId(sl), GetCurrentProcessId());
    HANDLE wd = spawn_plain("dxd_watchdog", extra);
    CHECK(wd != NULL);
    if (wd) {
        DWORD code = wait_exit(wd, 10000);
        CHECKF(code == 30, "watchdog exit code %lu", code);   /* contract of this corpus' watchdog: unopenable original => 30, no switching */
        if (code == 0xDEAD0001) TerminateProcess(wd, 1);
        CloseHandle(wd);
    }
    TerminateProcess(sl, 1); WaitForSingleObject(sl, 5000); CloseHandle(sl);
    WCHAR cur[128]; CHECK(dxd_input_name(cur, 128)); EXPECT_WSTR(cur, dxd_orig_name());
}

DXT(guard_watchdog_bad_pid_exits_31) {
    DXD_BEGIN();
    char nm[256], extra[320];
    WideCharToMultiByte(CP_ACP, 0, dxd_orig_name(), -1, nm, sizeof nm, NULL, NULL);
    snprintf(extra, sizeof extra, "%lu %s", 0x7FFFFFF0u, nm);   /* no such process */
    HANDLE wd = spawn_plain("dxd_watchdog", extra);
    CHECK(wd != NULL);
    if (wd) {
        DWORD code = wait_exit(wd, 10000);
        CHECKF(code == 31, "watchdog exit code %lu", code);
        if (code == 0xDEAD0001) TerminateProcess(wd, 1);
        CloseHandle(wd);
    }
}

DXT(guard_restores_after_switching_process_is_killed) {
    DXD_BEGIN();
    PROCESS_INFORMATION pi;
    HANDLE sw = t_spawn_child("dxd_switcher", "", CREATE_NO_WINDOW, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!sw) { t_skip("cannot spawn"); return; }
    char nm[256], extra[320];
    WideCharToMultiByte(CP_ACP, 0, dxd_orig_name(), -1, nm, sizeof nm, NULL, NULL);
    snprintf(extra, sizeof extra, "%lu %s", pi.dwProcessId, nm);
    HANDLE wd = spawn_plain("dxd_watchdog", extra);
    Sleep(700);                                   /* time for the switcher to create its desktop and switch */
    TerminateProcess(sw, 1);                      /* "crash" while (maybe) on the new desktop */
    WaitForSingleObject(sw, 5000);
    DWORD t0 = GetTickCount(); WCHAR cur[128]; int back = 0;
    while (GetTickCount() - t0 < 12000) {
        if (dxd_input_name(cur, 128) && !lstrcmpiW(cur, dxd_orig_name())) { back = 1; break; }
        Sleep(100);
    }
    CHECKF(back, "input desktop is \"%ls\", want \"%ls\"", cur, dxd_orig_name());
    if (!back) dxd_restore();
    if (wd) {
        DWORD code = wait_exit(wd, 10000);
        CHECKF(code == 0 || code == 10, "watchdog exit code %lu", code);   /* 10 = it had to restore, 0 = switch was denied or restored by the system */
        if (code == 0xDEAD0001) TerminateProcess(wd, 1);
        CloseHandle(wd);
    }
    CloseHandle(pi.hThread); CloseHandle(sw);
}

/* ================================ code-review assertions ========================================== */

/* The area must never import (or be able to call by name through its own import table) anything that
 * ends the session. All user32/advapi32 calls are made through LoadLibrary/GetProcAddress, so any
 * such import would have to be an explicit link-time dependency: assert there is none. */
static int import_present(const char *name) {
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dh = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dh->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd.VirtualAddress) return 0;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dd.VirtualAddress); d->Name; d++) {
        ULONG_PTR *thunk = (ULONG_PTR *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        for (; *thunk; thunk++) {
            if (*thunk & ((ULONG_PTR)1 << 63)) continue;      /* by ordinal */
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + *thunk);
            if (!lstrcmpA((const char *)ibn->Name, name)) return 1;
        }
    }
    return 0;
}

DXT(review_no_session_ending_imports) {
    static const char *const banned[] = {
        "ExitWindowsEx", "ExitWindows", "InitiateSystemShutdownA", "InitiateSystemShutdownW",
        "InitiateSystemShutdownExA", "InitiateSystemShutdownExW", "LockWorkStation", "SetSystemPowerState",
        "SetSuspendState", "WTSLogoffSession", "WTSDisconnectSession", "NtShutdownSystem", "NtSetSystemPowerState",
        "NtUserLockWorkStation", "NtUserSetWindowStationUser", "SwitchToThisWindow", "BlockInput", "SendInput",
        "keybd_event", "mouse_event", "SetCursorPos",
    };
    for (size_t i = 0; i < sizeof banned / sizeof banned[0]; i++)
        CHECKF(!import_present(banned[i]), "%s is imported", banned[i]);
}
