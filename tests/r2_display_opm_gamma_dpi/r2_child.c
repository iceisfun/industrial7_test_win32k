/* Process-wide DPI scenarios (children only - awareness is a one-way per-process setting).
 *
 * NtUserSetProcessDpiAwarenessContext(ctx, flag) in win32kbase:
 *   - IsValidKernelDpiAwarenessContext(ctx): valid are (ctx & 0x5fffffff) in {0x12, 0x22, 0x6010, 0x40006010}, or
 *     (ctx & 0x5ffe00ff) == 0x11 (system aware, DPI in bits 8..16); anything else -> UserSetLastError(0x57), return 0.
 *   - first successful call sets ppi flag 0x400000 and stores ctx in ppi+0x118. Afterwards a call succeeds only when flag != 0,
 *     ctx is not unaware and the stored value is 0x12 (per-monitor v1 -> v2 upgrade); everything else returns 0 without an error code.
 *   - NtUserGetProcessDpiAwarenessContext(0) returns the calling thread's effective context.
 * NtUserDeferWindowDpiChanges / RestoreWindowDpiChanges on a per-monitor window: first defer 1, second 0 (already deferred),
 *   restore 1, restore again 0 (nothing deferred); GetDpiForWindow reads the deferred word at tagWND+0x11e before +0x11c. */
#include "r2.h"

#ifndef DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED
#define DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED ((DPI_AWARENESS_CONTEXT)-5)
#endif
#define CC(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fail++; } } while (0)
#define CCF(c, ...) do { if (!(c)) { printf("FAIL %s:%d: %s (", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf(")\n"); fail++; } } while (0)

static void connect_gui(void) { if (U(GetDpiForSystem)) U(GetDpiForSystem)(); }
static unsigned short wnd16(HWND w, unsigned off, int *ok) { unsigned short v = 0; void *p = k_wnd_ptr(w); *ok = p && k_try_read_u16((char *)p + off, &v); return v; }
static unsigned long wnd32(HWND w, unsigned off, int *ok) { unsigned long v = 0; void *p = k_wnd_ptr(w); *ok = p && k_try_read_u32((char *)p + off, &v); return v; }

static int sc_invalid_contexts(void) {
    int fail = 0;
    connect_gui();
    static const ULONG bad[] = { 0, 1, 2, 0x10, 0x14, 0x21, 0x23, 0x32, 0x6012, 0x100, 0x1000, 0x6020, 0x40006011 | 0x80 };
    ULONG before = UN(NtUserGetProcessDpiAwarenessContext)(NULL);
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        BOOL r = UN(NtUserSetProcessDpiAwarenessContext)(bad[i], 0);
        CCF(r == 0, "ctx 0x%lx accepted", (unsigned long)bad[i]);
        CCF(GetLastError() == 0x57, "ctx 0x%lx error %lu", (unsigned long)bad[i], GetLastError());
        r = UN(NtUserSetProcessDpiAwarenessContext)(bad[i], 1);
        CCF(r == 0, "ctx 0x%lx (flag 1) accepted", (unsigned long)bad[i]);
    }
    CCF(UN(NtUserGetProcessDpiAwarenessContext)(NULL) == before, "context changed by rejected calls");
    SetLastError(0);
    CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == TRUE);    /* process was not initialised by the rejects */
    return fail;
}
static int sc_first_set_then_locked(void) {
    int fail = 0;
    connect_gui();
    CC(UN(NtUserSetProcessDpiAwarenessContext)(0x22, 0) == 1);
    CCF((UN(NtUserGetProcessDpiAwarenessContext)(NULL) & 0xf) == 2, "readback 0x%lx", (unsigned long)UN(NtUserGetProcessDpiAwarenessContext)(NULL));
    CC(UN(NtUserSetProcessDpiAwarenessContext)(0x22, 0) == 0);          /* flag already set, caller passed no upgrade flag */
    CC(UN(NtUserSetProcessDpiAwarenessContext)(0x12, 1) == 0);          /* stored context is not 0x12 */
    CC(UN(NtUserSetProcessDpiAwarenessContext)(0x6010, 1) == 0);        /* unaware can never be applied after init */
    CC(UN(NtUserSetProcessDpiAwarenessContext)(0x22, 1) == 0);
    CCF((UN(NtUserGetProcessDpiAwarenessContext)(NULL) & 0xf) == 2, "context changed by rejected calls");
    HWND w = k_make_window("r2_dpi_child");
    CC(w != NULL);
    if (w) {
        int ok; unsigned long ctx = wnd32(w, 0x120, &ok);
        if (ok) CCF((ctx & 0xf) == 2, "new window awareness word 0x%lx", ctx);
        CC(U(GetDpiForWindow)(w) >= 96);
        U(DestroyWindow)(w);
    }
    return fail;
}
static int sc_pmv1_upgrade_to_pmv2(void) {
    int fail = 0;
    connect_gui();
    CC(UN(NtUserSetProcessDpiAwarenessContext)(0x12, 0) == 1);
    ULONG rb = UN(NtUserGetProcessDpiAwarenessContext)(NULL);
    CCF((rb & 0xf) == 2, "readback 0x%lx", (unsigned long)rb);
    CC(UN(NtUserSetProcessDpiAwarenessContext)(0x12, 0) == 0);          /* no upgrade flag */
    CC(UN(NtUserSetProcessDpiAwarenessContext)(0x22, 0) == 0);
    CC(UN(NtUserSetProcessDpiAwarenessContext)(0x6010, 1) == 0);        /* unaware is not an upgrade */
    return fail;
}
static int sc_pmv1_upgrade_flag(void) {
    int fail = 0;
    connect_gui();
    CC(UN(NtUserSetProcessDpiAwarenessContext)(0x12, 0) == 1);
    CC(UN(NtUserSetProcessDpiAwarenessContext)(0x22, 1) == 1);          /* flag != 0, ctx not unaware, stored == 0x12: upgrade accepted */
    ULONG rb = UN(NtUserGetProcessDpiAwarenessContext)(NULL);
    CCF((rb & 0xf) == 2, "readback 0x%lx", (unsigned long)rb);
    printf("NOTE readback after upgrade 0x%lx\n", (unsigned long)rb);
    CC(UN(NtUserSetProcessDpiAwarenessContext)(0x12, 1) == 0);          /* stored value is no longer 0x12 */
    return fail;
}
static int sc_set_one(const char *arg) {
    int fail = 0;
    unsigned long ctx = strtoul(arg, NULL, 16);
    connect_gui();
    BOOL r = UN(NtUserSetProcessDpiAwarenessContext)((ULONG)ctx, 0);
    if (r != 1 && (ctx & 0xf) == 1) { printf("NOTE system-aware ctx 0x%lx refused: its DPI field must equal the process system DPI\n", ctx); return 0; }
    CCF(r == 1, "ctx 0x%lx rejected (error %lu)", ctx, GetLastError());
    if (r != 1) return fail;
    ULONG rb = UN(NtUserGetProcessDpiAwarenessContext)(NULL);
    unsigned want = ((ctx & 0x5fffffff) == 0x6010 || (ctx & 0x5fffffff) == 0x40006010) ? 0 : (unsigned)(ctx & 0xf);
    CCF((rb & 0xf) == want, "readback 0x%lx want awareness nibble %u", (unsigned long)rb, want);
    printf("NOTE ctx 0x%lx readback 0x%lx\n", ctx, (unsigned long)rb);
    HWND w = k_make_window("r2_dpi_child");
    CC(w != NULL);
    if (w) {
        int ok; unsigned long wc = wnd32(w, 0x120, &ok);
        if (ok) CCF((wc & 0xf) == want, "window awareness word 0x%lx want nibble %u", wc, want);
        UINT d = U(GetDpiForWindow)(w);
        if (want == 0) CCF(d == 96, "unaware window DPI %u", d);
        else CCF(d >= 96, "window DPI %u", d);
        if (want == 1) CCF(d == U(GetDpiForSystem)(), "system-aware window DPI %u vs system %u", d, U(GetDpiForSystem)());
        U(DestroyWindow)(w);
    }
    return fail;
}
static int sc_defer_restore(void) {
    int fail = 0;
    connect_gui();
    if (!U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) { printf("NOTE cannot set PMv2\n"); return 0; }
    HWND w = k_make_window("r2_defer");
    CC(w != NULL);
    if (!w) return fail;
    int ok;
    unsigned long ctx = wnd32(w, 0x120, &ok);
    if (ok) CCF((ctx & 0xf) == 2, "window awareness word 0x%lx", ctx);
    unsigned short cur = wnd16(w, 0x11c, &ok), def0 = wnd16(w, 0x11e, &ok);
    UINT dpi0 = U(GetDpiForWindow)(w);
    CCF(dpi0 >= 96, "dpi %u", dpi0);
    if (def0 == 0) {                                                    /* nothing deferred yet: restore is a silent no-op */
        SetLastError(0);
        CC(UN(NtUserRestoreWindowDpiChanges)(w) == 0);
        CCF(GetLastError() == 0, "error %lu on harmless restore", GetLastError());
    } else printf("NOTE window starts with deferred word 0x%x; skipping the no-op restore check\n", def0);
    CC(UN(NtUserDeferWindowDpiChanges)(w) == 1);
    CC(UN(NtUserDeferWindowDpiChanges)(w) == 0);                        /* already deferred */
    unsigned short def1 = wnd16(w, 0x11e, &ok);
    if (ok) printf("NOTE deferred word before 0x%x after 0x%x (cur 0x%x)\n", def0, def1, cur);
    CCF(U(GetDpiForWindow)(w) == dpi0, "GetDpiForWindow changed from %u to %u by deferring at the same DPI", dpi0, U(GetDpiForWindow)(w));
    CC(UN(NtUserRestoreWindowDpiChanges)(w) == 1);
    CC(UN(NtUserRestoreWindowDpiChanges)(w) == 0);
    CCF(wnd16(w, 0x11e, &ok) == 0, "deferred word not cleared by restore");
    CC(UN(NtUserDeferWindowDpiChanges)(w) == 1);                        /* can defer again after restoring */
    CC(UN(NtUserRestoreWindowDpiChanges)(w) == 1);
    U(DestroyWindow)(w);
    return fail;
}
static int sc_defer_child_window(void) {
    int fail = 0;
    connect_gui();
    if (!U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return 0;
    HWND w = k_make_window("r2_defer_parent");
    CC(w != NULL);
    if (!w) return fail;
    HWND c = U(CreateWindowExA)(0, "win32ktHelperWnd", "r2_child", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, w, NULL, GetModuleHandleA(NULL), NULL);
    CC(c != NULL);
    CC(UN(NtUserDeferWindowDpiChanges)(w) == 1);
    int ok; unsigned short a = wnd16(w, 0x11e, &ok), b = c ? wnd16(c, 0x11e, &ok) : 0;
    printf("NOTE parent deferred word 0x%x child 0x%x\n", a, b);
    CC(UN(NtUserRestoreWindowDpiChanges)(w) == 1);
    if (c) CCF(wnd16(c, 0x11e, &ok) == 0, "child's deferred word not cleared with the parent");
    if (c) U(DestroyWindow)(c);
    U(DestroyWindow)(w);
    return fail;
}
static int sc_gdiscaled(void) {
    int fail = 0;
    connect_gui();
    if (!U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED)) { printf("NOTE UNAWARE_GDISCALED unsupported (error %lu)\n", GetLastError()); return 0; }
    CC(U(GetAwarenessFromDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED) == DPI_AWARENESS_UNAWARE);
    HWND w = k_make_window("r2_gdiscaled");
    CC(w != NULL);
    if (!w) return fail;
    int ok; unsigned long ctx = wnd32(w, 0x120, &ok);
    int scaled = ok && (ctx & 0x4000000f) == 0x40000000;
    printf("NOTE window ctx word 0x%lx (readable %d)\n", ctx, ok);
    BOOL is0 = UN(NtUserIsWindowGDIScaledDpiMessageEnabled)(w);
    CCF(is0 == 0, "enabled by default: %d", is0);
    BOOL en = UN(NtUserEnableWindowGDIScaledDpiMessage)(w, TRUE);
    BOOL is1 = UN(NtUserIsWindowGDIScaledDpiMessageEnabled)(w);
    if (!scaled) { CCF(en == 0 && is1 == 0, "window is not GDI-scaled but enable=%d is=%d", en, is1); }
    printf("NOTE enable(TRUE)=%d is=%d\n", en, is1);
    if (en) {                                                           /* the enable path stores bit 29 of tagWND+0xe8 */
        CCF(is1 == 1, "enabled but query says %d", is1);
        unsigned long e8 = wnd32(w, 0xe8, &ok);
        if (ok) CCF((e8 & 0x20000000) != 0, "tagWND+0xe8 = 0x%lx lacks bit 29", e8);
        BOOL en2 = UN(NtUserEnableWindowGDIScaledDpiMessage)(w, FALSE);
        CCF(en2 == 1, "disable returned %d", en2);
        CCF(UN(NtUserIsWindowGDIScaledDpiMessageEnabled)(w) == 0, "still enabled after disable");
        e8 = wnd32(w, 0xe8, &ok);
        if (ok) CCF((e8 & 0x20000000) == 0, "tagWND+0xe8 = 0x%lx still has bit 29", e8);
    } else {
        CCF(is1 == 0, "not enabled but query says %d", is1);
    }
    SetLastError(0);
    CC(UN(NtUserEnableWindowGDIScaledDpiMessage)((HWND)(ULONG_PTR)0x1234, TRUE) == 0);
    CCF(GetLastError() == 1400, "error %lu", GetLastError());
    U(DestroyWindow)(w);
    return fail;
}
static int sc_gdiscaled_plain_process_pm(void) {
    int fail = 0;
    connect_gui();
    if (!U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) return 0;
    HWND w = k_make_window("r2_gdiscaled_pm");
    CC(w != NULL);
    if (!w) return fail;
    CC(UN(NtUserEnableWindowGDIScaledDpiMessage)(w, TRUE) == 0);       /* per-monitor window: (ctx & 0x4000000f) != 0x40000000 */
    CC(UN(NtUserIsWindowGDIScaledDpiMessageEnabled)(w) == 0);
    U(DestroyWindow)(w);
    return fail;
}
static int sc_dialog_behavior_pm(void) {
    int fail = 0;
    connect_gui();
    if (!U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return 0;
    HWND w = k_make_window("r2_dlg");
    CC(w != NULL);
    if (!w) return fail;
    typedef USHORT (WINAPI *GDB)(HWND);
    GDB get = (GDB)t_proc_in("user32.dll", "GetDialogControlDpiChangeBehavior");
    CC(UN(NtUserSetDialogControlDpiChangeBehavior)(w, 3, 3) == 1);
    if (get) CCF((get(w) & 3) == 3, "bits %u", get(w) & 3);
    CC(UN(NtUserDeferWindowDpiChanges)(w) == 1);
    if (get) CCF((get(w) & 3) == 3, "defer changed dialog behaviour bits to %u", get(w) & 3);
    CC(UN(NtUserRestoreWindowDpiChanges)(w) == 1);
    U(DestroyWindow)(w);
    return fail;
}
static int sc_point_scaling_pmv2(void) {
    int fail = 0;
    connect_gui();
    if (!U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return 0;
    HWND w = k_make_window("r2_pt");
    CC(w != NULL);
    if (!w) return fail;
    POINT p = { 100, 200 };
    BOOL r = UN(NtUserLogicalToPhysicalDpiPointForWindow)(w, &p);
    CCF(!(r == 0) || (p.x == 100 && p.y == 200), "failure return but point (%ld,%ld) moved", p.x, p.y);
    POINT q = { 100, 200 };
    r = UN(NtUserPhysicalToLogicalDpiPointForWindow)(w, &q);
    CCF(!(r == 0) || (q.x == 100 && q.y == 200), "failure return but point moved");
    printf("NOTE PMv2 l2p -> (%ld,%ld) p2l -> (%ld,%ld)\n", p.x, p.y, q.x, q.y);
    U(DestroyWindow)(w);
    return fail;
}

CHILD(r2_display_opm_gamma_dpi_child) {
    const char *sel = argc > 1 ? argv[1] : "";
    if (!strcmp(sel, "invalid_contexts")) return sc_invalid_contexts();
    if (!strcmp(sel, "first_set_then_locked")) return sc_first_set_then_locked();
    if (!strcmp(sel, "pmv1_upgrade_to_pmv2")) return sc_pmv1_upgrade_to_pmv2();
    if (!strcmp(sel, "pmv1_upgrade_flag")) return sc_pmv1_upgrade_flag();
    if (!strncmp(sel, "set:", 4)) return sc_set_one(sel + 4);
    if (!strcmp(sel, "defer_restore")) return sc_defer_restore();
    if (!strcmp(sel, "defer_child_window")) return sc_defer_child_window();
    if (!strcmp(sel, "gdiscaled")) return sc_gdiscaled();
    if (!strcmp(sel, "gdiscaled_pm")) return sc_gdiscaled_plain_process_pm();
    if (!strcmp(sel, "dialog_behavior_pm")) return sc_dialog_behavior_pm();
    if (!strcmp(sel, "point_scaling_pmv2")) return sc_point_scaling_pmv2();
    printf("FAIL unknown scenario '%s'\n", sel);
    return 1;
}

static void run_sc(const char *name) {
    if (t_is_wine()) { t_skip("Wine: process-wide DPI scenarios are Windows-only"); return; }
    struct k_child_result r = k_child_run("r2_display_opm_gamma_dpi_child", name, 60000);
    CHECKF(r.exit_code == 0, "child scenario %s exit code 0x%lx (0 = clean, else its failure count / crash code)", name, (unsigned long)r.exit_code);
}
#define SC(tname, scn) T2(proc_##tname) { NEED_GUI(); NEED_NT(NtUserSetProcessDpiAwarenessContext); run_sc(scn); }
SC(invalid_kernel_contexts_rejected_with_57, "invalid_contexts")
SC(first_direct_set_locks_further_changes, "first_set_then_locked")
SC(pmv1_second_call_without_flag_refused, "pmv1_upgrade_to_pmv2")
SC(pmv1_upgrade_with_flag_accepted, "pmv1_upgrade_flag")
SC(set_kernel_pmv1_0x12, "set:12")
SC(set_kernel_pmv2_0x22, "set:22")
SC(set_kernel_unaware_0x6010, "set:6010")
SC(set_kernel_unaware_gdiscaled_0x40006010, "set:40006010")
SC(set_kernel_system_aware_0x6011, "set:6011")
SC(defer_restore_per_monitor_window, "defer_restore")
SC(defer_restore_covers_child_windows, "defer_child_window")
SC(gdiscaled_message_toggle, "gdiscaled")
SC(gdiscaled_message_refused_for_per_monitor_window, "gdiscaled_pm")
SC(dialog_behavior_survives_defer, "dialog_behavior_pm")
SC(point_scaling_calls_in_pmv2_process, "point_scaling_pmv2")
