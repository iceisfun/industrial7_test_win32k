/* win32k_mitigation / parent: the parent inspects a parked child from outside.
 *
 * The child parks (mit_park: signals an event, waits for the parent) and the parent looks at it:
 *  - remote policy readback through a process handle (NtQueryInformationProcess 0x34 needs only
 *    PROCESS_QUERY_LIMITED_INFORMATION; setting through a handle is refused for policy 4);
 *  - user32!GetGuiResources -> win32kfull!NtUserGetGuiResources: for a process handle it takes
 *    PsGetProcessWin32Process(target) and fails with UserSetLastError(0x57) when that W32PROCESS is NULL, which is
 *    exactly the state of a locked (or never-converted) process; flags above 4 fail the same way up front;
 *  - the child's PEB/TEB read with ReadProcessMemory: the kernel-written win32 fields are NULL for the locked child
 *    and non-NULL for a converted control. */
#include "mit.h"

SCN(park_idle) { mit_park(); }
SCN(park_gui) {
    MIT_NEED_WIN32K();
    HDC (WINAPI *gdc)(HWND) = (HDC (WINAPI *)(HWND))t_proc_in("user32.dll", "GetDC");
    HDC (WINAPI *ccdc)(HDC) = (HDC (WINAPI *)(HDC))t_proc_in("gdi32.dll", "CreateCompatibleDC");
    MC(gdc && ccdc);
    HDC dc = gdc ? gdc(NULL) : NULL;
    HDC mem = ccdc ? ccdc(dc) : NULL;
    MC(dc != NULL && mem != NULL);
    mit_park();
    BOOL (WINAPI *dd)(HDC) = (BOOL (WINAPI *)(HDC))t_proc_in("gdi32.dll", "DeleteDC");
    if (dd && mem) dd(mem);
}

typedef DWORD (WINAPI *ggr_fn)(HANDLE, DWORD);

static int start_parked(struct mit_proc *p, const char *scn, const char *flavor, const struct mit_attr *a) {
    char sel[256];
    snprintf(sel, sizeof sel, "%s::%s", scn, flavor);
    if (!mit_start(p, sel, a, 1)) {
        if (t_is_wine()) t_skip("spawn failed under Wine (%lu)", p->err); else CHECKF(0, "CreateProcess failed: %lu", p->err);
        return 0;
    }
    if (!mit_wait_ready(p, 15000)) {
        if (t_is_wine()) t_skip("child not ready under Wine"); else CHECKF(0, "child never reached its park point");
        mit_release(p); mit_finish(p, 5000);
        return 0;
    }
    return 1;
}
static void end_parked(struct mit_proc *p) {
    mit_release(p);
    DWORD code = mit_finish(p, 20000);
    CHECKF(code == 0, "child exit code 0x%lx", code);
}

/* ---- remote policy ------------------------------------------------------------------------------ */
static void remote_policy(const char *flavor, const struct mit_attr *a, DWORD want) {
    struct mit_proc p; DWORD fl = ~0UL;
    if (!start_parked(&p, "park_idle", flavor, a)) return;
    CHECK(mit_get_scd(p.proc, &fl)); EXPECT_EQ(fl, want);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
    if (CHECK(h != NULL), h) {
        fl = ~0UL; CHECK(mit_get_scd(h, &fl)); EXPECT_EQ(fl, want);
        fl = ~0UL; EXPECT_EQ((ULONG)mit_nt_get(h, MIT_POLICY_SCD, &fl), ST_SUCCESS); EXPECT_EQ(fl, want);
        /* a handle other than -1 is refused for policy 4 before any other test */
        EXPECT_EQ((ULONG)mit_nt_set(h, MIT_POLICY_SCD, MIT_SCD_DISALLOW), ST_INVALID_PARAMETER);
        EXPECT_EQ((ULONG)mit_nt_set(h, MIT_POLICY_SCD, 0), ST_INVALID_PARAMETER);
        CloseHandle(h);
    }
    h = OpenProcess(SYNCHRONIZE, FALSE, p.pid);
    if (h) {
        SET_ERR(0); CHECK(!mit_get_scd(h, &fl)); EXPECT_ERR(ERROR_ACCESS_DENIED);   /* PROCESS_QUERY_LIMITED_INFORMATION missing */
        CloseHandle(h);
    }
    fl = ~0UL; CHECK(mit_get_scd(p.proc, &fl)); EXPECT_EQ(fl, want);   /* the remote probes changed nothing */
    end_parked(&p);
}
TEST(win32k_mitigation_parent_remote_policy_set) {
    struct mit_proc p; DWORD fl;
    if (t_is_wine()) { t_skip("no policy under Wine"); return; }
    if (!start_parked(&p, "park_idle", "set", NULL)) return;
    CHECK(mit_get_scd(p.proc, &fl)); EXPECT_EQ(fl, 1);
    end_parked(&p);
}
TEST(win32k_mitigation_parent_remote_policy_create) { if (t_is_wine()) { t_skip("no policy under Wine"); return; } remote_policy("cre", &MIT_ATTR_ON8, 1); }
TEST(win32k_mitigation_parent_remote_policy_plain) { if (t_is_wine()) { t_skip("no policy under Wine"); return; } remote_policy("plain", NULL, 0); }
TEST(win32k_mitigation_parent_remote_policy_audit_created) {
    struct mit_proc p; DWORD fl = ~0UL;
    if (t_is_wine()) { t_skip("no policy under Wine"); return; }
    char sel[64]; snprintf(sel, sizeof sel, "park_idle::audc");
    if (!mit_start(&p, sel, &MIT_ATTR_AUDIT, 1)) { t_note("audit attribute rejected: %lu", p.err); t_skip("no audit attribute"); return; }
    CHECK(mit_wait_ready(&p, 15000));
    CHECK(mit_get_scd(p.proc, &fl)); EXPECT_EQ(fl, 2);
    end_parked(&p);
}
TEST(win32k_mitigation_parent_two_children_independent) {
    struct mit_proc a, b; DWORD fa = ~0UL, fb = ~0UL;
    if (t_is_wine()) { t_skip("no policy under Wine"); return; }
    if (!start_parked(&a, "park_idle", "cre", &MIT_ATTR_ON8)) return;
    if (!start_parked(&b, "park_idle", "plain", NULL)) { end_parked(&a); return; }
    CHECK(mit_get_scd(a.proc, &fa)); CHECK(mit_get_scd(b.proc, &fb));
    EXPECT_EQ(fa, 1); EXPECT_EQ(fb, 0);
    EXPECT_NE(a.pid, b.pid);
    end_parked(&a); end_parked(&b);
}
TEST(win32k_mitigation_parent_policy_own_unchanged_by_children) {
    struct mit_proc p; DWORD fl = ~0UL;
    if (t_is_wine()) { t_skip("no policy under Wine"); return; }
    if (!start_parked(&p, "park_idle", "cre", &MIT_ATTR_ON8)) return;
    CHECK(mit_get_scd(GetCurrentProcess(), &fl)); EXPECT_EQ(fl, 0);       /* a locked child does not lock its parent */
    end_parked(&p);
}
TEST(win32k_mitigation_parent_terminate_locked_child) {
    struct mit_proc p;
    if (t_is_wine()) { t_skip("no policy under Wine"); return; }
    if (!start_parked(&p, "park_idle", "cre", &MIT_ATTR_ON8)) return;
    CHECK(TerminateProcess(p.proc, 0x1234));
    DWORD code = mit_finish(&p, 10000);
    EXPECT_EQ(code, 0x1234);                                              /* teardown of a process without a W32PROCESS */
}
TEST(win32k_mitigation_parent_session_matches) {
    struct mit_proc p; DWORD sess = ~0UL;
    if (!start_parked(&p, "park_idle", "plain", NULL)) return;
    CHECK(ProcessIdToSessionId(p.pid, &sess));
    EXPECT_EQ(sess, k_session_id());
    end_parked(&p);
}
TEST(win32k_mitigation_parent_locked_child_session_matches) {
    struct mit_proc p; DWORD sess = ~0UL;
    if (t_is_wine()) { t_skip("no policy under Wine"); return; }
    if (!start_parked(&p, "park_idle", "cre", &MIT_ATTR_ON8)) return;
    CHECK(ProcessIdToSessionId(p.pid, &sess));
    EXPECT_EQ(sess, k_session_id());     /* lockdown does not move the process out of the interactive session */
    end_parked(&p);
}

/* ---- GetGuiResources ------------------------------------------------------------------------------- */
static void guires_all_fail(const char *flavor, const struct mit_attr *a) {
    ggr_fn g = UIN("user32.dll", GetGuiResources);
    struct mit_proc p;
    REQUIRE(g);
    if (t_is_wine()) { t_skip("Wine GetGuiResources is not win32k"); return; }
    if (!start_parked(&p, "park_idle", flavor, a)) return;
    for (DWORD flag = 0; flag <= 4; flag++) {
        SET_ERR(0);
        DWORD r = g(p.proc, flag);
        CHECKF(r == 0, "flag %lu -> %lu", flag, r);
        /* a process without a W32PROCESS answers 0 and leaves the last error alone for the valid flags (Windows 10) */
        CHECKF(GetLastError() == 0, "flag %lu GetLastError %lu", flag, GetLastError());
    }
    static const DWORD bad[] = { 5, 6, 100, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECKF(g(p.proc, bad[i]) == 0, "flag %lu", bad[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "flag %lu GetLastError %lu", bad[i], GetLastError());  /* 4 < uiFlags */
    }
    end_parked(&p);
}
TEST(win32k_mitigation_parent_guiresources_locked_set) { guires_all_fail("set", NULL); }
TEST(win32k_mitigation_parent_guiresources_locked_create) { guires_all_fail("cre", &MIT_ATTR_ON8); }
TEST(win32k_mitigation_parent_guiresources_never_converted) { guires_all_fail("plain", NULL); }
TEST(win32k_mitigation_parent_guiresources_converted_control) {
    ggr_fn g = UIN("user32.dll", GetGuiResources);
    struct mit_proc p;
    REQUIRE(g);
    if (t_is_wine()) { t_skip("Wine GetGuiResources is not win32k"); return; }
    if (!start_parked(&p, "park_gui", "plain", NULL)) return;
    SET_ERR(0);
    DWORD gdi = g(p.proc, GR_GDIOBJECTS), gdip = g(p.proc, 2 /* GR_GDIOBJECTS_PEAK */);
    DWORD usr = g(p.proc, GR_USEROBJECTS), usrp = g(p.proc, 4 /* GR_USEROBJECTS_PEAK */);
    CHECKF(gdi >= 1, "GDI objects %lu", gdi);                              /* NtUserGetGuiResources reads W32PROCESS+0x3c */
    CHECKF(gdip >= gdi, "GDI peak %lu < %lu", gdip, gdi);
    CHECKF(usrp >= usr, "USER peak %lu < %lu", usrp, usr);
    EXPECT_ERR(0);                                                          /* success path never sets the error */
    t_note("converted control child: gdi=%lu peak=%lu user=%lu peak=%lu", gdi, gdip, usr, usrp);
    end_parked(&p);
}
TEST(win32k_mitigation_parent_guiresources_self) {
    ggr_fn g = UIN("user32.dll", GetGuiResources);
    REQUIRE(g);
    if (t_is_wine()) { t_skip("Wine GetGuiResources is not win32k"); return; }
    SET_ERR(0);
    DWORD a = g(GetCurrentProcess(), GR_GDIOBJECTS);
    EXPECT_ERR(0);
    CHECKF(a >= 0, "ok"); (void)a;
}

/* ---- remote PEB / TEB / modules ----------------------------------------------------------------------- */
typedef LONG (WINAPI *ntqip_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef struct { LONG ExitStatus; PVOID TebBaseAddress; ULONG_PTR pid, tid, aff; LONG prio, bprio; } mit_tbi;

struct remote { unsigned long long peb58, pebF8; unsigned long peb108; unsigned long long teb78; int ci_nonzero; int ok; };
static int read_remote(struct mit_proc *p, struct remote *r) {
    ntqip_fn qip = (ntqip_fn)mit_nt("NtQueryInformationProcess"), qit = (ntqip_fn)mit_nt("NtQueryInformationThread");
    PROCESS_BASIC_INFORMATION pbi; mit_tbi tbi; SIZE_T got; unsigned char ci[0x1F0];
    memset(r, 0, sizeof *r);
    if (!qip || !qit) return 0;
    if (qip(p->proc, 0, &pbi, sizeof pbi, NULL) < 0 || !pbi.PebBaseAddress) return 0;
    if (qit(p->thread, 0, &tbi, sizeof tbi, NULL) < 0 || !tbi.TebBaseAddress) return 0;
    if (!ReadProcessMemory(p->proc, (char *)pbi.PebBaseAddress + PEB_KernelCallbackTable, &r->peb58, 8, &got)) return 0;
    if (!ReadProcessMemory(p->proc, (char *)pbi.PebBaseAddress + PEB_GdiSharedHandleTable, &r->pebF8, 8, &got)) return 0;
    if (!ReadProcessMemory(p->proc, (char *)pbi.PebBaseAddress + PEB_GdiDCAttributeList, &r->peb108, 4, &got)) return 0;
    if (!ReadProcessMemory(p->proc, (char *)tbi.TebBaseAddress + TEB_Win32ThreadInfo, &r->teb78, 8, &got)) return 0;
    if (!ReadProcessMemory(p->proc, (char *)tbi.TebBaseAddress + TEB_Win32ClientInfo, ci, sizeof ci, &got)) return 0;
    for (unsigned i = 0; i < sizeof ci; i++) r->ci_nonzero += ci[i] != 0;
    r->ok = 1;
    return 1;
}
static void remote_state_locked(const char *flavor, const struct mit_attr *a) {
    struct mit_proc p; struct remote r;
    if (t_is_wine()) { t_skip("no policy under Wine"); return; }
    if (!start_parked(&p, "park_idle", flavor, a)) return;
    CHECK(read_remote(&p, &r));
    if (r.ok) {
        EXPECT_EQ(r.peb58, 0);         /* PEB.KernelCallbackTable: only user32's init writes it */
        EXPECT_EQ(r.pebF8, 0);         /* PEB.GdiSharedHandleTable: written by GdiProcessCallout, never run */
        EXPECT_EQ(r.peb108, 0);        /* PEB.GdiDCAttributeList */
        EXPECT_EQ(r.teb78, 0);         /* TEB.Win32ThreadInfo of the child's main thread */
        EXPECT_EQ(r.ci_nonzero, 0);    /* TEB.Win32ClientInfo untouched by InitClientInfo */
    }
    end_parked(&p);
}
TEST(win32k_mitigation_parent_remote_state_locked_set) { remote_state_locked("set", NULL); }
TEST(win32k_mitigation_parent_remote_state_locked_create) { remote_state_locked("cre", &MIT_ATTR_ON8); }
TEST(win32k_mitigation_parent_remote_state_never_converted) { remote_state_locked("plain", NULL); }
TEST(win32k_mitigation_parent_remote_state_converted_control) {
    struct mit_proc p; struct remote r;
    if (t_is_wine()) { t_skip("no win32k under Wine"); return; }
    if (!start_parked(&p, "park_gui", "plain", NULL)) return;
    CHECK(read_remote(&p, &r));
    if (r.ok) {
        CHECK(r.peb58 != 0);           /* user32 DllMain installed the callback table */
        CHECK(r.pebF8 != 0);           /* GdiProcessCallout mapped the shared handle table */
        CHECK(r.peb108 != 0);          /* GdiDCAttributeList = 0x14 */
        CHECK(r.teb78 != 0);           /* converted main thread */
        CHECK(r.ci_nonzero != 0);      /* InitClientInfo filled CLIENTINFO */
    }
    end_parked(&p);
}

typedef BOOL (WINAPI *enummod_fn)(HANDLE, HMODULE *, DWORD, LPDWORD, DWORD);
typedef DWORD (WINAPI *modname_fn)(HANDLE, HMODULE, LPSTR, DWORD);
static int remote_has_module(struct mit_proc *p, const char *name, int *total) {
    enummod_fn em = (enummod_fn)t_proc_in("kernel32.dll", "K32EnumProcessModulesEx");
    modname_fn mn = (modname_fn)t_proc_in("kernel32.dll", "K32GetModuleBaseNameA");
    HMODULE mods[512]; DWORD need = 0; int found = 0;
    if (!em || !mn || !em(p->proc, mods, sizeof mods, &need, 3 /* LIST_MODULES_ALL */)) return -1;
    DWORD n = need / sizeof(HMODULE); if (n > 512) n = 512;
    if (total) *total = (int)n;
    for (DWORD i = 0; i < n; i++) { char b[MAX_PATH]; if (mn(p->proc, mods[i], b, sizeof b) && !lstrcmpiA(b, name)) found = 1; }
    return found;
}
static void modules_locked(const char *flavor, const struct mit_attr *a) {
    struct mit_proc p; int total = 0;
    if (t_is_wine()) { t_skip("no policy under Wine"); return; }
    if (!start_parked(&p, "park_idle", flavor, a)) return;
    static const char *gui[] = { "user32.dll", "gdi32.dll", "gdi32full.dll", "win32u.dll" };
    for (unsigned i = 0; i < sizeof gui / sizeof *gui; i++) {
        int f = remote_has_module(&p, gui[i], &total);
        CHECKF(f == 0, "%s present in the parked child (%d)", gui[i], f);
    }
    CHECK(remote_has_module(&p, "ntdll.dll", NULL) == 1);
    CHECK(remote_has_module(&p, "kernel32.dll", NULL) == 1);
    t_note("%d modules in the parked %s child", total, flavor);
    end_parked(&p);
}
TEST(win32k_mitigation_parent_modules_locked_set) { modules_locked("set", NULL); }
TEST(win32k_mitigation_parent_modules_locked_create) { modules_locked("cre", &MIT_ATTR_ON8); }
TEST(win32k_mitigation_parent_modules_converted_control) {
    struct mit_proc p;
    if (t_is_wine()) { t_skip("no win32k under Wine"); return; }
    if (!start_parked(&p, "park_gui", "plain", NULL)) return;
    CHECK(remote_has_module(&p, "user32.dll", NULL) == 1);
    CHECK(remote_has_module(&p, "gdi32.dll", NULL) == 1);
    CHECK(remote_has_module(&p, "win32u.dll", NULL) == 1);
    end_parked(&p);
}
