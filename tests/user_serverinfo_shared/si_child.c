/* Pristine-process and cross-process checks of the SERVERINFO page. Children never ran any U() before
 * their entry point (the harness child path does not load modules), so they observe the state a
 * brand new process sees before and right after win32k connects it. */
#include "si.h"

/* Under headless Wine a child spawned after the parent connected to Wine's user32 stalls until the timeout; the
 * children only assert Windows-specific state anyway, so do not spawn them there once user32 is loaded. */
static int si_wine_no_child(void) {
    if (t_is_wine() && GetModuleHandleA("user32.dll")) { t_skip("Wine: child hangs once the parent has connected to user32; Windows-only assertions"); return 1; }
    return 0;
}
#define CCHK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fail++; } } while (0)
#define CCHKF(c, ...) do { if (!(c)) { printf("FAIL %s:%d: %s (", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf(")\n"); fail++; } } while (0)

static unsigned long long si_hash_u32(unsigned long long h, unsigned long v) { return (h ^ v) * 1099511628211ULL; }
static void si_page_hashes(char *psi, unsigned long long *hm, unsigned long long *hc, unsigned long long *hf) {
    unsigned long long a = 1469598103934665603ULL, b = a, f = a;
    for (int i = 0; i <= SI_SM_LAST; i++) {
        if (si_cache_idx(i) >= 0) continue; /* DPI-dependent entries of aiSysMet are not the served values */
        a = si_hash_u32(a, si_u32(psi, SI_aiSysMet + (size_t)i * 4));
    }
    for (int i = 0; i < SI_COLOR_COUNT; i++) b = si_hash_u32(b, si_u32(psi, SI_argbSystem + (size_t)i * 4));
    f = si_hash_u32(f, si_u32(psi, SI_dwSRVIFlags) & 0xe);
    *hm = a; *hc = b; *hf = f;
}

/* ---- 1. a fresh process: no thread info before user32, a full SERVERINFO right after ---- */
CHILD(user_serverinfo_shared_pristine) {
    (void)argc; (void)argv;
    int fail = 0;
    void *ti = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &ti);
    if (ti != NULL) {
        if (t_is_wine()) printf("NOTE Wine: Win32ThreadInfo already %p in a fresh process\n", ti);
        else { printf("FAIL pristine: TEB.Win32ThreadInfo=%p before any win32k call, want NULL\n", ti); fail++; }
    }
    FARPROC gsm = t_proc("GetSystemMetrics");
    if (!gsm) { printf("FAIL pristine: GetSystemMetrics unresolvable\n"); return 1; }
    int cx = ((int (WINAPI *)(int))gsm)(SM_CXSCREEN);
    if (t_is_wine()) { printf("NOTE Wine: skipping page checks\n"); return fail; }
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &ti);
    CCHK(ti != NULL);
    struct k_user_shared_info s;
    CCHK(k_shared_info(&s));
    char *psi = (char *)s.psi;
    CCHK(psi != NULL);
    if (!psi) return fail + 1;
    unsigned long v;
    CCHK(k_try_read_u32(psi, &v));
    CCHKF(cx > 0, "SM_CXSCREEN=%d", cx);
    /* the very first GetSystemMetrics already reads the connected page */
    CCHK(si_i32(psi, SI_aiSysMet + 4 * SM_CXBORDER) == ((int (WINAPI *)(int))gsm)(SM_CXBORDER));
    FARPROC gsc = t_proc("GetSysColor");
    CCHK(gsc != NULL);
    for (int i = 0; gsc && i < SI_COLOR_COUNT; i++)
        CCHKF(((DWORD (WINAPI *)(int))gsc)(i) == si_u32(psi, SI_argbSystem + (size_t)i * 4), "color %d", i);
    unsigned long long n = 0;
    CCHK(k_try_read(psi + SI_cHandleEntries, &n, 8) && n >= 16 && n <= 0x10000);
    MEMORY_BASIC_INFORMATION mbi;
    CCHK(VirtualQuery(psi, &mbi, sizeof mbi) == sizeof mbi);
    CCHK(mbi.Type == MEM_MAPPED);
    CCHK((mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0);
    void *cb = k_cb_table();
    CCHK(cb != NULL);
    return fail;
}
TEST(user_serverinfo_shared_pristine_process_sees_connected_page) {
    if (si_wine_no_child()) return;
    struct k_child_result r = k_child_run("user_serverinfo_shared_pristine", NULL, 30000);
    CHECKF(r.exit_code == 0, "child exit=0x%lx", (unsigned long)r.exit_code);
}

/* ---- 2. a second process in the same session reads the same page contents ---- */
CHILD(user_serverinfo_shared_crossproc) {
    int fail = 0;
    if (argc < 4) { printf("FAIL crossproc: need 3 hash args\n"); return 1; }
    unsigned long long wm = strtoull(argv[1], NULL, 16), wc = strtoull(argv[2], NULL, 16), wf = strtoull(argv[3], NULL, 16);
    FARPROC gsm = t_proc("GetSystemMetrics");
    if (!gsm) return 1;
    ((int (WINAPI *)(int))gsm)(0);
    if (t_is_wine()) { printf("NOTE Wine: skipping\n"); return 0; }
    struct k_user_shared_info s;
    if (!k_shared_info(&s) || !s.psi) { printf("FAIL crossproc: no psi\n"); return 1; }
    unsigned long long hm, hc, hf;
    si_page_hashes((char *)s.psi, &hm, &hc, &hf);
    CCHKF(hm == wm, "aiSysMet hash child=%llx parent=%llx", hm, wm);
    CCHKF(hc == wc, "argbSystem hash child=%llx parent=%llx", hc, wc);
    CCHKF(hf == wf, "flags hash child=%llx parent=%llx", hf, wf);
    return fail;
}
TEST(user_serverinfo_shared_second_process_sees_same_page_contents) {
    char *psi = si_get(); if (!psi) return;
    unsigned long long hm, hc, hf;
    si_page_hashes(psi, &hm, &hc, &hf);
    char args[128];
    snprintf(args, sizeof args, "%llx %llx %llx", hm, hc, hf);
    /* SERVERINFO is one session-wide section: every process in the session maps the same bytes. */
    if (si_wine_no_child()) return;
    struct k_child_result r = k_child_run("user_serverinfo_shared_crossproc", args, 30000);
    CHECKF(r.exit_code == 0, "child exit=0x%lx", (unsigned long)r.exit_code);
}

/* ---- 3. a DPI-aware process takes the system-DPI slot and unscaled monitor rectangles ---- */
CHILD(user_serverinfo_shared_aware) {
    (void)argc; (void)argv;
    int fail = 0;
    FARPROC sa = t_proc("SetProcessDPIAware");
    FARPROC gsm = t_proc("GetSystemMetrics");
    FARPROC gds = t_proc("GetDpiForSystem");
    if (!sa || !gsm || !gds) { printf("FAIL aware: exports missing\n"); return 1; }
    ((BOOL (WINAPI *)(void))sa)();
    int (WINAPI *GSM)(int) = (int (WINAPI *)(int))gsm;
    GSM(0);
    if (t_is_wine()) { printf("NOTE Wine: skipping\n"); return 0; }
    struct k_user_shared_info s;
    if (!k_shared_info(&s) || !s.psi) { printf("FAIL aware: no psi\n"); return 1; }
    char *psi = (char *)s.psi;
    unsigned pd = si_u16(psi, SI_SystemDpi);
    UINT d = ((UINT (WINAPI *)(void))gds)();
    /* system-aware: GetDpiForSystem returns the process DPI == the page's system DPI, so the cache is addressed by slot 0 */
    CCHKF(d == pd, "GetDpiForSystem=%u page=%u", d, pd);
    CCHK(si_cache_slot(d, pd) == 0);
    long c0 = si_i32(psi, SI_DpiCache + 0 * 4);
    CCHKF(c0 == GSM(SM_CXVSCROLL), "slot0[0]=%ld SM_CXVSCROLL=%d", c0, GSM(SM_CXVSCROLL));
    long c2 = si_i32(psi, SI_DpiCache + 2 * 4);
    CCHKF(c2 == GSM(SM_CYCAPTION), "slot0[2]=%ld SM_CYCAPTION=%d", c2, GSM(SM_CYCAPTION));
    FARPROC mfp = t_proc("MonitorFromPoint"), gmi = t_proc("GetMonitorInfoW");
    if (mfp && gmi) {
        POINT pt = {0, 0};
        HMONITOR m = ((HMONITOR (WINAPI *)(POINT, DWORD))mfp)(pt, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof mi };
        CCHK(((BOOL (WINAPI *)(HMONITOR, LPMONITORINFO))gmi)(m, &mi));
        char *obj = si_monitor_obj(m);
        RECT raw;
        CCHK(obj && k_try_read(obj + SI_MON_rcMonitor, &raw, sizeof raw));
        if (obj) {
            /* system-aware callers get the physical monitor rectangle verbatim from the monitor object */
            CCHK(raw.left == mi.rcMonitor.left && raw.right == mi.rcMonitor.right && raw.top == mi.rcMonitor.top && raw.bottom == mi.rcMonitor.bottom);
            CCHKF(GSM(SM_CXSCREEN) == raw.right - raw.left, "SM_CXSCREEN=%d raw=%ld", GSM(SM_CXSCREEN), raw.right - raw.left);
            CCHK(GSM(SM_CYSCREEN) == raw.bottom - raw.top);
        }
    }
    FARPROC gdb = t_proc("GetDialogBaseUnits");
    if (gdb) {
        LONG u = ((LONG (WINAPI *)(void))gdb)();
        CCHKF((unsigned)LOWORD(u) == si_u16(psi, SI_DpiServerInfo + SI_DSI_cxChar) && (unsigned)HIWORD(u) == si_u16(psi, SI_DpiServerInfo + SI_DSI_cyChar),
              "dialog units %d,%d slot0 %u,%u", LOWORD(u), HIWORD(u), si_u16(psi, SI_DpiServerInfo + SI_DSI_cxChar), si_u16(psi, SI_DpiServerInfo + SI_DSI_cyChar));
    }
    FARPROC gdc = t_proc("GetDC"), gcaps = t_proc("GetDeviceCaps"), rdc = t_proc("ReleaseDC");
    if (gdc && gcaps && rdc) {
        HDC dc = ((HDC (WINAPI *)(HWND))gdc)(NULL);
        int lx = ((int (WINAPI *)(HDC, int))gcaps)(dc, LOGPIXELSX);
        ((int (WINAPI *)(HWND, HDC))rdc)(NULL, dc);
        CCHKF((unsigned)lx == pd, "LOGPIXELSX=%d page=%u", lx, pd);
    }
    FARPROC ntd = t_proc_in("win32u.dll", "NtUserGetDpiForCurrentProcess");
    if (ntd) CCHKF((((ULONG (WINAPI *)(void))ntd)() & 0xffff) == pd, "NtUserGetDpiForCurrentProcess");
    return fail;
}
TEST(user_serverinfo_shared_system_aware_process_uses_slot_zero) {
    if (si_wine_no_child()) return;
    struct k_child_result r = k_child_run("user_serverinfo_shared_aware", NULL, 30000);
    CHECKF(r.exit_code == 0, "child exit=0x%lx", (unsigned long)r.exit_code);
}

/* ---- 4. second child: the page of a process that never created a window ---- */
CHILD(user_serverinfo_shared_nowindow) {
    (void)argc; (void)argv;
    int fail = 0;
    FARPROC glii = t_proc("GetLastInputInfo");
    FARPROC gsm = t_proc("GetSystemMetrics");
    if (!glii || !gsm) return 1;
    ((int (WINAPI *)(int))gsm)(0);
    if (t_is_wine()) return 0;
    struct k_user_shared_info s;
    if (!k_shared_info(&s) || !s.psi) return 1;
    LASTINPUTINFO li = { sizeof li, 0 };
    CCHK(((BOOL (WINAPI *)(PLASTINPUTINFO))glii)(&li));
    unsigned long pg = si_u32((char *)s.psi, SI_LastRITTick);
    CCHKF((int)(pg - li.dwTime) >= 0, "page tick %lu before API tick %lu", pg, (unsigned long)li.dwTime);
    void *ti = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &ti);
    CCHK(ti != NULL);
    return fail;
}
TEST(user_serverinfo_shared_windowless_process_reads_last_input) {
    if (si_wine_no_child()) return;
    struct k_child_result r = k_child_run("user_serverinfo_shared_nowindow", NULL, 30000);
    CHECKF(r.exit_code == 0, "child exit=0x%lx", (unsigned long)r.exit_code);
}
