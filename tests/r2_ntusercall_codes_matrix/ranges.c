/* Code-space partition, stub numbers, out-of-range and invalid-HWND matrices for all NtUserCall*
 * dispatchers. Per-code semantics live in noparam.c / oneparam.c / twoparam.c / hwnd.c. */
#include "mx.h"

struct disp { const char *name; DWORD lo, hi; };
/* The primary partition; each range is the exact "code - lo < n" test in the win32kfull analysis. */
static const struct disp g_part[] = {
    { "NtUserCallNoParam",       0x00, 0x2b },
    { "NtUserCallOneParam",      0x2c, 0x53 },
    { "NtUserCallHwnd",          0x54, 0x5b },
    { "NtUserCallHwndOpt",       0x5c, 0x5d },
    { "NtUserCallHwndParam",     0x5e, 0x68 },
    { "NtUserCallHwndLock",      0x69, 0x75 },
    { "NtUserCallHwndParamLock", 0x76, 0x7d },
    { "NtUserCallTwoParam",      0x7e, 0x8d },
};
#define NPART (sizeof g_part / sizeof g_part[0])

/* (dispatcher, code) pairs emitted by the real user32 callers (win32u callers analysed). */
struct caller { const char *disp; DWORD code; const char *api; };
static const struct caller g_callers[] = {
    { "NtUserCallNoParam", 0x00, "CreateMenu" }, { "NtUserCallNoParam", 0x01, "CreatePopupMenu" },
    { "NtUserCallNoParam", 0x02, "AllowForegroundActivation" }, { "NtUserCallNoParam", 0x06, "DestroyCaret" },
    { "NtUserCallNoParam", 0x07, "DisableProcessWindowsGhosting" }, { "NtUserCallNoParam", 0x08, "DrainThreadCoreMessagingCompletions" },
    { "NtUserCallNoParam", 0x0b, "GetInputDesktop" }, { "NtUserCallNoParam", 0x0c, "GetMessagePos" },
    { "NtUserCallNoParam", 0x0e, "GetUnpredictedMessagePos" }, { "NtUserCallNoParam", 0x11, "IsQueueAttached" },
    { "NtUserCallNoParam", 0x13, "GetSystemMetrics(prologue)" }, { "NtUserCallNoParam", 0x16, "ReleaseCapture" },
    { "NtUserCallNoParam", 0x29, "EnableMouseInPointerForThread" }, { "NtUserCallNoParam", 0x2b, "EnablePerMonitorMenuScaling" },
    { "NtUserCallOneParam", 0x2c, "BeginDeferWindowPos" }, { "NtUserCallOneParam", 0x2d, "GetSendMessageReceiver" },
    { "NtUserCallOneParam", 0x2e, "AllowSetForegroundWindow" }, { "NtUserCallOneParam", 0x31, "EnumClipboardFormats" },
    { "NtUserCallOneParam", 0x33, "GetKeyboardType" }, { "NtUserCallOneParam", 0x34, "GetProcessDefaultLayout" },
    { "NtUserCallOneParam", 0x36, "LockSetForegroundWindow" }, { "NtUserCallOneParam", 0x38, "ValidateHwnd/HMValidateHandle" },
    { "NtUserCallOneParam", 0x39, "MessageBeep" }, { "NtUserCallOneParam", 0x3b, "PostQuitMessage" },
    { "NtUserCallOneParam", 0x42, "ReplyMessage" }, { "NtUserCallOneParam", 0x43, "SetCaretBlinkTime" },
    { "NtUserCallOneParam", 0x44, "SetDoubleClickTime" }, { "NtUserCallOneParam", 0x45, "SetMessageExtraInfo" },
    { "NtUserCallOneParam", 0x46, "SetProcessDefaultLayout" }, { "NtUserCallOneParam", 0x49, "SwapMouseButton" },
    { "NtUserCallOneParam", 0x4c, "EnableSessionForMMCSS" }, { "NtUserCallOneParam", 0x4e, "IsThreadMessageQueueAttached" },
    { "NtUserCallOneParam", 0x50, "RealGetSystemMetrics" }, { "NtUserCallOneParam", 0x53, "SetShellChangeNotifyWindow" },
    { "NtUserCallHwnd", 0x54, "DeregisterShellHookWindow" }, { "NtUserCallHwnd", 0x57, "GetWindowContextHelpId" },
    { "NtUserCallHwnd", 0x58, "RegisterShellHookWindow" }, { "NtUserCallHwnd", 0x5b, "ScheduleDispatchNotification" },
    { "NtUserCallHwndOpt", 0x5c, "SetProgmanWindow" }, { "NtUserCallHwndOpt", 0x5d, "SetTaskmanWindow" },
    { "NtUserCallHwndParam", 0x5e, "_GetClassData" }, { "NtUserCallHwndParam", 0x65, "SetWindowContextHelpId" },
    { "NtUserCallHwndParam", 0x68, "EnableModernAppWindowKeyboardIntercept" },
    { "NtUserCallHwndLock", 0x69, "ArrangeIconicWindows" }, { "NtUserCallHwndLock", 0x6a, "DrawMenuBar" },
    { "NtUserCallHwndLock", 0x70, "SetForegroundWindow" }, { "NtUserCallHwndLock", 0x73, "UpdateWindow" },
    { "NtUserCallHwndLock", 0x75, "GetWindowTrackInfoAsync" },
    { "NtUserCallHwndParamLock", 0x79, "ShowOwnedPopups" }, { "NtUserCallHwndParamLock", 0x7a, "SwitchToThisWindow" },
    { "NtUserCallHwndParamLock", 0x7c, "ValidateRgn" },
    { "NtUserCallHwndParamLockSafe", 0x7d, "EnableWindow" }, { "NtUserCallHwndLockSafe", 0x6b, "ImeSetContextHandler" },
    { "NtUserCallHwndSafe", 0x5a, "InitThreadCoreMessagingIocp" },
    { "NtUserCallTwoParam", 0x7e, "ChangeWindowMessageFilter" }, { "NtUserCallTwoParam", 0x7f, "GetCursorPos" },
    { "NtUserCallTwoParam", 0x88, "SetCaretPos" }, { "NtUserCallTwoParam", 0x8a, "UnhookWindowsHook" },
    { "NtUserCallTwoParam", 0x8d, "RealGetSystemMetricsForDpi" },
};

static int in_disp(const char *name, DWORD code) {
    /* resolves the sub-range dispatchers as well */
    if (!strcmp(name, "NtUserCallHwndSafe")) return code == 0x5a || code == 0x5b;
    if (!strcmp(name, "NtUserCallHwndLockSafe")) return code == 0x6b;
    if (!strcmp(name, "NtUserCallHwndParamLockSafe")) return code == 0x7d;
    for (size_t i = 0; i < NPART; i++)
        if (!strcmp(g_part[i].name, name)) return code >= g_part[i].lo && code <= g_part[i].hi;
    return 0;
}

T(ranges_partition_is_contiguous) {
    /* Pure arithmetic over the analysis range checks: each dispatcher starts right after the previous. */
    CHECK(g_part[0].lo == 0);
    for (size_t i = 1; i < NPART; i++)
        CHECKF(g_part[i].lo == g_part[i - 1].hi + 1, "%s starts at 0x%lx, previous ends at 0x%lx",
               g_part[i].name, (unsigned long)g_part[i].lo, (unsigned long)g_part[i - 1].hi);
    CHECK(g_part[NPART - 1].hi == 0x8d);
}

T(ranges_every_user32_caller_code_is_inside_its_dispatcher) {
    for (size_t i = 0; i < sizeof g_callers / sizeof g_callers[0]; i++)
        CHECKF(in_disp(g_callers[i].disp, g_callers[i].code), "%s code 0x%lx (%s) outside the dispatcher range",
               g_callers[i].disp, (unsigned long)g_callers[i].code, g_callers[i].api);
}

T(ranges_caller_codes_are_unique_per_dispatcher_family) {
    /* The same code never appears under two different primary dispatchers (a code belongs to exactly
     * one range of the partition), except the documented Safe/LockSafe aliases of a primary one. */
    size_t n = sizeof g_callers / sizeof g_callers[0];
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            if (g_callers[i].code == g_callers[j].code) {
                int safe = strstr(g_callers[i].disp, "Safe") || strstr(g_callers[j].disp, "Safe");
                CHECKF(safe, "code 0x%lx shared by %s and %s", (unsigned long)g_callers[i].code, g_callers[i].disp, g_callers[j].disp);
            }
}

/* ---- stub service numbers (tools/win32u_syscalls.tsv, 19041) ---------------------------------- */
static const struct { const char *name; long num; } g_stubs[] = {
    { "NtUserCallOneParam", 4098 }, { "NtUserCallNoParam", 4101 }, { "NtUserCallMsgFilter", 4117 },
    { "NtUserCallHwndLock", 4129 }, { "NtUserCallHwndParamLock", 4135 }, { "NtUserCallTwoParam", 4138 },
    { "NtUserCallHwndParam", 4251 }, { "NtUserCallHwnd", 4351 }, { "NtUserCallHwndLockSafe", 5030 },
    { "NtUserCallHwndOpt", 5031 }, { "NtUserCallHwndParamLockSafe", 5032 }, { "NtUserCallHwndSafe", 5033 },
};

T(ranges_stub_service_numbers) {
    if (t_is_wine()) { t_skip("Wine win32u numbering differs"); return; }
    for (size_t i = 0; i < sizeof g_stubs / sizeof g_stubs[0]; i++) {
        FARPROC p = t_proc_in("win32u.dll", g_stubs[i].name);
        if (!p) { t_note("%s not exported", g_stubs[i].name); continue; }
        CHECKF(k_stub_valid(p), "%s stub prologue", g_stubs[i].name);
        long n = k_stub_number(p);
        if (k_is_19041()) CHECKF(n == g_stubs[i].num, "%s service number %ld want %ld", g_stubs[i].name, n, g_stubs[i].num);
        else t_note("%s service number %ld (19041: %ld)", g_stubs[i].name, n, g_stubs[i].num);
        /* all of them live in the win32k (>= 0x1000) service table */
        CHECKF(n >= 0x1000, "%s number 0x%lx not a win32k service", g_stubs[i].name, n);
    }
}

T(ranges_all_stubs_exported_by_win32u) {
    for (size_t i = 0; i < sizeof g_stubs / sizeof g_stubs[0]; i++)
        CHECKF(t_proc_in("win32u.dll", g_stubs[i].name) != NULL, "win32u!%s missing", g_stubs[i].name);
}

/* ---- out-of-range codes: return 0, last error untouched, no state change ------------------- */


T(ranges_noparam_out_of_range_inert) {
    NEED_RAW(NtUserCallNoParam);
    static const DWORD codes[] = { 0x2c, 0x2d, 0x40, 0x53, 0x54, 0x7e, 0x8d, 0x8e, 0x100, 0xffff, 0x10000, 0x80000000u, 0xfffffffeu, 0xffffffffu };
    struct mx_snap a, b; mx_snap_take(&a);
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        SetLastError(SENTINEL_ERR);
        ULONG_PTR r = NP(codes[i]);
        DWORD e = GetLastError();
        CHECKF(r == 0, "NoParam(0x%lx) = %p", (unsigned long)codes[i], (void *)r);
        CHECKF(e == SENTINEL_ERR, "NoParam(0x%lx) changed last error to %lu", (unsigned long)codes[i], e);
    }
    mx_snap_take(&b);
    CHECK(mx_snap_equal(&a, &b));
}

T(ranges_oneparam_out_of_range_inert) {
    NEED_RAW(NtUserCallOneParam);
    /* NoParam, Hwnd* and TwoParam codes are all outside "code - 0x2c < 0x28": no dispatch, so the
     * argument is never looked at (win32kfull!NtUserCallOneParam returns 0 before EnterCrit). */
    static const DWORD codes[] = { 0, 1, 2, 6, 0x0c, 0x16, 0x2b, 0x54, 0x57, 0x5c, 0x65, 0x69, 0x70, 0x7d, 0x7e, 0x7f, 0x88, 0x8d, 0x8e, 0x1000, 0xffffffffu };
    static const ULONG_PTR args[] = { 0, 1, 0x1234, (ULONG_PTR)-1 };
    struct mx_snap a, b; mx_snap_take(&a);
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++)
        for (size_t j = 0; j < sizeof args / sizeof args[0]; j++) {
            SetLastError(SENTINEL_ERR);
            ULONG_PTR r = OP(args[j], codes[i]);
            CHECKF(r == 0, "OneParam(0x%llx, 0x%lx) = %p", (unsigned long long)args[j], (unsigned long)codes[i], (void *)r);
            CHECKF(GetLastError() == SENTINEL_ERR, "OneParam(0x%lx) changed last error", (unsigned long)codes[i]);
        }
    mx_snap_take(&b);
    CHECK(mx_snap_equal(&a, &b));
}

T(ranges_twoparam_out_of_range_inert) {
    NEED_RAW(NtUserCallTwoParam);
    /* NtUserCallTwoParam still enters/leaves the critical section but skips the table call. */
    static const DWORD codes[] = { 0, 1, 6, 0x16, 0x2c, 0x38, 0x54, 0x70, 0x7d, 0x8e, 0x8f, 0x100, 0xffff, 0xffffffffu };
    struct mx_snap a, b; mx_snap_take(&a);
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        SetLastError(SENTINEL_ERR);
        ULONG_PTR r = TP(1, 2, codes[i]);
        CHECKF(r == 0, "TwoParam(1,2,0x%lx) = %p", (unsigned long)codes[i], (void *)r);
        CHECKF(GetLastError() == SENTINEL_ERR, "TwoParam(0x%lx) changed last error", (unsigned long)codes[i]);
    }
    mx_snap_take(&b);
    CHECK(mx_snap_equal(&a, &b));
}

/* Codes just inside/outside every boundary of the partition, fed to the *wrong* dispatcher. */
T(ranges_boundary_codes_to_neighbour_dispatchers) {
    NEED_RAW(NtUserCallNoParam); REQUIRE(UN(NtUserCallOneParam)); REQUIRE(UN(NtUserCallTwoParam));
    SetLastError(SENTINEL_ERR);
    CHECK(NP(0x2c) == 0);                 /* first OneParam code is out of NoParam's range */
    CHECK(OP(0, 0x2b) == 0);              /* last NoParam code is out of OneParam's range */
    CHECK(OP(0, 0x54) == 0);              /* first Hwnd code is out of OneParam's range */
    CHECK(TP(0, 0, 0x7d) == 0);           /* last ParamLock code is out of TwoParam's range */
    CHECK(NP(0x7e) == 0);
    CHECKF(GetLastError() == SENTINEL_ERR, "last error changed to %lu", GetLastError());
}

/* ---- Hwnd family with a valid window and a code from another dispatcher's range ---------------- */

struct hwnd_oor { const char *name; const DWORD *codes; size_t n; };

T(ranges_hwnd_family_out_of_range_inert_with_valid_window) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_oor");
    if (!w) { t_skip("no window"); return; }
    static const DWORD c_hwnd[]  = { 0, 0x16, 0x2c, 0x53, 0x5c, 0x65, 0x69, 0x7e, 0x8d, 0x8e, 0x1000, 0xffffffffu };
    static const DWORD c_opt[]   = { 0, 0x16, 0x54, 0x5b, 0x5e, 0x65, 0x69, 0x7d, 0x7e, 0x1000, 0xffffffffu };
    static const DWORD c_safe[]  = { 0, 0x54, 0x59, 0x5c, 0x65, 0x69, 0x7d, 0x7e, 0xffffffffu };
    static const DWORD c_lock[]  = { 0, 0x16, 0x54, 0x5c, 0x65, 0x68, 0x76, 0x77, 0x7d, 0x7e, 0x8d, 0xffffffffu };
    static const DWORD c_lsafe[] = { 0, 0x54, 0x69, 0x6a, 0x6c, 0x71, 0x73, 0x7d, 0x7e };
    static const DWORD c_param[] = { 0, 0x16, 0x54, 0x5b, 0x5c, 0x5d, 0x69, 0x6a, 0x76, 0x7d, 0x7e, 0x8d };
    static const DWORD c_plock[] = { 0, 0x16, 0x54, 0x68, 0x69, 0x6a, 0x71, 0x73, 0x7e, 0x8d };
    static const DWORD c_plsafe[]= { 0, 0x54, 0x69, 0x6a, 0x76, 0x79, 0x7c, 0x7e, 0x8d };
    struct mx_snap a, b; mx_snap_take(&a);
    LONG_PTR style = U(GetWindowLongPtrA)(w, GWL_STYLE);
    for (size_t i = 0; i < sizeof c_hwnd / sizeof c_hwnd[0]; i++)  { SetLastError(SENTINEL_ERR); CHECKF(HW(w, c_hwnd[i]) == 0, "Hwnd 0x%lx", (unsigned long)c_hwnd[i]); CHECK(GetLastError() == SENTINEL_ERR); }
    for (size_t i = 0; i < sizeof c_opt / sizeof c_opt[0]; i++)    { SetLastError(SENTINEL_ERR); CHECKF(HWO(w, c_opt[i]) == 0, "HwndOpt 0x%lx", (unsigned long)c_opt[i]); CHECK(GetLastError() == SENTINEL_ERR);
                                                                    SetLastError(SENTINEL_ERR); CHECKF(HWO(NULL, c_opt[i]) == 0, "HwndOpt(NULL) 0x%lx", (unsigned long)c_opt[i]); CHECK(GetLastError() == SENTINEL_ERR); }
    for (size_t i = 0; i < sizeof c_safe / sizeof c_safe[0]; i++)  { SetLastError(SENTINEL_ERR); CHECKF(HWS(w, c_safe[i]) == 0, "HwndSafe 0x%lx", (unsigned long)c_safe[i]); CHECK(GetLastError() == SENTINEL_ERR); }
    for (size_t i = 0; i < sizeof c_lock / sizeof c_lock[0]; i++)  { SetLastError(SENTINEL_ERR); CHECKF(HWL(w, c_lock[i]) == 0, "HwndLock 0x%lx", (unsigned long)c_lock[i]); CHECK(GetLastError() == SENTINEL_ERR); }
    for (size_t i = 0; i < sizeof c_lsafe / sizeof c_lsafe[0]; i++){ SetLastError(SENTINEL_ERR); CHECKF(HWLS(w, c_lsafe[i]) == 0, "HwndLockSafe 0x%lx", (unsigned long)c_lsafe[i]); CHECK(GetLastError() == SENTINEL_ERR); }
    for (size_t i = 0; i < sizeof c_param / sizeof c_param[0]; i++){ SetLastError(SENTINEL_ERR); CHECKF(HWP(w, 0, c_param[i]) == 0, "HwndParam 0x%lx", (unsigned long)c_param[i]); CHECK(GetLastError() == SENTINEL_ERR); }
    for (size_t i = 0; i < sizeof c_plock / sizeof c_plock[0]; i++){ SetLastError(SENTINEL_ERR); CHECKF(HWPL(w, 1, c_plock[i]) == 0, "HwndParamLock 0x%lx", (unsigned long)c_plock[i]); CHECK(GetLastError() == SENTINEL_ERR); }
    for (size_t i = 0; i < sizeof c_plsafe / sizeof c_plsafe[0]; i++){ SetLastError(SENTINEL_ERR); CHECKF(HWPLS(w, 1, c_plsafe[i]) == 0, "HwndParamLockSafe 0x%lx", (unsigned long)c_plsafe[i]); CHECK(GetLastError() == SENTINEL_ERR); }
    mx_snap_take(&b);
    CHECK(mx_snap_equal(&a, &b));
    CHECK(U(GetWindowLongPtrA)(w, GWL_STYLE) == style);
    CHECK(U(IsWindow)(w));
    CHECK(U(IsWindowEnabled)(w));
    U(DestroyWindow)(w);
}

/* ---- invalid HWND matrix: 1400 for every dispatcher and every code, valid or not ----------- */

enum { VAR_NULL, VAR_UNIQ0, VAR_DESTROYED, VAR_MENU, VAR_ACCEL, VAR_UNIQ_FLIPPED, VAR_COUNT };
static const char *const g_varname[] = { "NULL", "uniq0", "destroyed", "menu", "accel", "uniq-flipped" };

struct invalid_set { HWND h[VAR_COUNT]; HMENU menu; HACCEL accel; HWND live; };

static int make_invalid(struct invalid_set *s) {
    memset(s, 0, sizeof *s);
    s->live = k_make_window("r2ncm_inv");
    if (!s->live) { t_note("fixture: live window not created (error %lu)", GetLastError()); return 0; }
    HWND d = k_make_window("r2ncm_dead");
    if (!d) { t_note("fixture: second window not created (error %lu)", GetLastError()); return 0; }
    U(DestroyWindow)(d);
    s->menu = U(CreateMenu)();
    /* zeroed buffer with room after the entry: the kernel looks at the padding byte of the 6-byte ACCEL, so an uninitialised
     * stack struct makes CreateAcceleratorTableW fail with ERROR_NOACCESS (998) */
    ACCEL ac[2]; memset(ac, 0, sizeof ac);
    ac[0].fVirt = FVIRTKEY; ac[0].key = 'A'; ac[0].cmd = 1;
    s->accel = U(CreateAcceleratorTableW)(ac, 1);
    ULONG_PTR lv = (ULONG_PTR)s->live;
    s->h[VAR_NULL] = NULL;
    s->h[VAR_UNIQ0] = (HWND)(lv & 0xffff);                       /* x64 process: uniq 0 is only accepted for WOW64 */
    s->h[VAR_DESTROYED] = d;
    s->h[VAR_MENU] = (HWND)s->menu;
    s->h[VAR_ACCEL] = (HWND)s->accel;
    WORD u = (WORD)(lv >> 16) ^ 0x5a5a;
    if (u == 0 || u == 0xffff) u = 0x1234;                       /* neither 0 nor the -1 wildcard */
    s->h[VAR_UNIQ_FLIPPED] = (HWND)((lv & 0xffff) | ((ULONG_PTR)u << 16));
    if (!s->menu || !s->accel) t_note("fixture: menu %p accelerator table %p (error %lu)", (void *)s->menu, (void *)s->accel, GetLastError());
    return s->menu && s->accel;
}
static void free_invalid(struct invalid_set *s) {
    if (s->live) U(DestroyWindow)(s->live);
    if (s->menu) U(DestroyMenu)(s->menu);
    if (s->accel) U(DestroyAcceleratorTable)(s->accel);
}

static void expect_invalid(const char *what, DWORD code, int var, ULONG_PTR r) {
    DWORD e = GetLastError();
    CHECKF(r == 0, "%s code 0x%lx handle=%s returned %p", what, (unsigned long)code, g_varname[var], (void *)r);
    CHECKF(e == ERR_INVALID_WINDOW_HANDLE, "%s code 0x%lx handle=%s last error %lu want 1400", what, (unsigned long)code, g_varname[var], e);
}

#define MATRIX(label, lo, hi, CALL, SKIPNULL)                                           \
    for (int v = 0; v < VAR_COUNT; v++) {                                               \
        if ((SKIPNULL) && v == VAR_NULL) continue;                                      \
        HWND hh = s.h[v];                                                               \
        for (DWORD c = (lo) - 1; c <= (hi) + 1; c++) {                                  \
            SetLastError(SENTINEL_ERR); ULONG_PTR r = (CALL); expect_invalid(label, c, v, r); } \
    }

T(ranges_invalid_hwnd_matrix_hwnd) {
    NEED_RAW(NtUserCallHwnd);
    struct invalid_set s; if (!make_invalid(&s)) { t_skip("fixture"); free_invalid(&s); return; }
    MATRIX("Hwnd", 0x54, 0x5b, HW(hh, c), 0)
    SetLastError(SENTINEL_ERR); expect_invalid("Hwnd", 0x1000, VAR_DESTROYED, HW(s.h[VAR_DESTROYED], 0x1000));
    free_invalid(&s);
}
T(ranges_invalid_hwnd_matrix_hwnd_safe) {
    NEED_RAW(NtUserCallHwndSafe);
    struct invalid_set s; if (!make_invalid(&s)) { t_skip("fixture"); free_invalid(&s); return; }
    MATRIX("HwndSafe", 0x5a, 0x5b, HWS(hh, c), 0)
    free_invalid(&s);
}
T(ranges_invalid_hwnd_matrix_hwnd_opt_nonnull) {
    NEED_RAW(NtUserCallHwndOpt);
    /* HwndOpt skips validation for NULL only; every non-NULL invalid handle still fails validation
     * (only a NULL handle skips the validation), so 0x5c/0x5d never dispatch here. */
    struct invalid_set s; if (!make_invalid(&s)) { t_skip("fixture"); free_invalid(&s); return; }
    MATRIX("HwndOpt", 0x5c, 0x5d, HWO(hh, c), 1)
    free_invalid(&s);
}
T(ranges_invalid_hwnd_matrix_hwnd_param) {
    NEED_RAW(NtUserCallHwndParam);
    struct invalid_set s; if (!make_invalid(&s)) { t_skip("fixture"); free_invalid(&s); return; }
    /* codes 0x5e / 0xffffffff only clear gbValidateHandleForIL before validating; c ranges over
     * 0x5d..0x69, so 0x5e is included on purpose (validation still fails first). */
    MATRIX("HwndParam", 0x5e, 0x68, HWP(hh, 0, c), 0)
    MATRIX("HwndParam(p=1)", 0x5e, 0x68, HWP(hh, 1, c), 0)
    free_invalid(&s);
}
T(ranges_invalid_hwnd_matrix_hwnd_lock) {
    NEED_RAW(NtUserCallHwndLock);
    struct invalid_set s; if (!make_invalid(&s)) { t_skip("fixture"); free_invalid(&s); return; }
    /* 0x7a/0x7b are intentionally outside the swept window 0x68..0x76: for those two codes
     * NtUserCallHwndLock does some unrelated work before it validates anything. */
    MATRIX("HwndLock", 0x69, 0x75, HWL(hh, c), 0)
    free_invalid(&s);
}
T(ranges_invalid_hwnd_matrix_hwnd_lock_safe) {
    NEED_RAW(NtUserCallHwndLockSafe);
    struct invalid_set s; if (!make_invalid(&s)) { t_skip("fixture"); free_invalid(&s); return; }
    MATRIX("HwndLockSafe", 0x6b, 0x6b, HWLS(hh, c), 0)
    free_invalid(&s);
}
T(ranges_invalid_hwnd_matrix_hwnd_param_lock) {
    NEED_RAW(NtUserCallHwndParamLock);
    struct invalid_set s; if (!make_invalid(&s)) { t_skip("fixture"); free_invalid(&s); return; }
    MATRIX("HwndParamLock", 0x76, 0x7d, HWPL(hh, 0, c), 0)
    free_invalid(&s);
}
T(ranges_invalid_hwnd_matrix_hwnd_param_lock_safe) {
    NEED_RAW(NtUserCallHwndParamLockSafe);
    struct invalid_set s; if (!make_invalid(&s)) { t_skip("fixture"); free_invalid(&s); return; }
    MATRIX("HwndParamLockSafe", 0x7d, 0x7d, HWPLS(hh, 1, c), 0)
    free_invalid(&s);
}

/* EnableWindow/ShowOwnedPopups/ArrangeIconicWindows/ValidateRgn/DrawMenuBar/UpdateWindow public
 * wrappers with a destroyed window: same 1400 (cross-check of the userland path). */
T(ranges_public_wrappers_report_1400_for_destroyed_window) {
    NEED_ALL_HWND();
    HWND d = k_make_window("r2ncm_dead2");
    if (!d) { t_skip("no window"); return; }
    U(DestroyWindow)(d);
    SetLastError(SENTINEL_ERR); CHECK(U(EnableWindow)(d, TRUE) == 0);          CHECK(GetLastError() == ERR_INVALID_WINDOW_HANDLE);
    SetLastError(SENTINEL_ERR); CHECK(U(ShowOwnedPopups)(d, TRUE) == 0);       CHECK(GetLastError() == ERR_INVALID_WINDOW_HANDLE);
    SetLastError(SENTINEL_ERR); CHECK(U(ArrangeIconicWindows)(d) == 0);        CHECK(GetLastError() == ERR_INVALID_WINDOW_HANDLE);
    SetLastError(SENTINEL_ERR); CHECK(U(DrawMenuBar)(d) == 0);                 CHECK(GetLastError() == ERR_INVALID_WINDOW_HANDLE);
    SetLastError(SENTINEL_ERR); CHECK(U(ValidateRgn)(d, NULL) == 0);           CHECK(GetLastError() == ERR_INVALID_WINDOW_HANDLE);
    SetLastError(SENTINEL_ERR); CHECK(U(GetWindowContextHelpId)(d) == 0);      CHECK(GetLastError() == ERR_INVALID_WINDOW_HANDLE);
    SetLastError(SENTINEL_ERR); CHECK(U(SetWindowContextHelpId)(d, 5) == 0);   CHECK(GetLastError() == ERR_INVALID_WINDOW_HANDLE);
}

/* ---- accepted handle spellings: observable through the context help id ----------------------- */

static ULONG_PTR help_via(HWND h) { return HW(h, 0x57); }

T(ranges_handle_spellings_accepted_by_validatehwnd) {
    NEED_RAW(NtUserCallHwnd);
    HWND w = k_make_window("r2ncm_spell");
    if (!w) { t_skip("no window"); return; }
    CHECK(U(SetWindowContextHelpId)(w, 0x2468));
    ULONG_PTR lv = (ULONG_PTR)w;
    SetLastError(SENTINEL_ERR);
    CHECK(LO32(help_via(w)) == 0x2468);                                     /* canonical handle */
    /* handle validation: a 0xFFFF uniq (the high word) is a wildcard. */
    HWND wild = (HWND)((lv & 0xffff) | 0xffff0000ULL);
    SetLastError(SENTINEL_ERR);
    CHECKF(LO32(help_via(wild)) == 0x2468, "wildcard-uniq handle %p rejected, err %lu", wild, GetLastError());
    /* Only bits 0..31 of the 64-bit argument are ever examined: the low word (index) and the high word (uniq) as a short. */
    HWND hi = (HWND)(lv | 0x1234567800000000ULL);
    SetLastError(SENTINEL_ERR);
    CHECKF(LO32(help_via(hi)) == 0x2468, "handle with garbage in bits 32..63 rejected, err %lu", GetLastError());
    HWND sx = (HWND)(ULONG_PTR)(LONG_PTR)(LONG)(DWORD)lv;                    /* sign-extended spelling */
    SetLastError(SENTINEL_ERR);
    CHECKF(LO32(help_via(sx)) == 0x2468, "sign-extended handle rejected, err %lu", GetLastError());
    /* uniq of 0 is rejected unless the process is WOW64 (PsGetCurrentProcessWow64Process() != 0). */
    HWND z = (HWND)(lv & 0xffff);
    SetLastError(SENTINEL_ERR);
    CHECK(help_via(z) == 0);
    CHECK(GetLastError() == ERR_INVALID_WINDOW_HANDLE);
    U(DestroyWindow)(w);
}

T(ranges_handle_spellings_rejected_when_uniq_differs) {
    NEED_RAW(NtUserCallHwnd);
    HWND w = k_make_window("r2ncm_spell2");
    if (!w) { t_skip("no window"); return; }
    CHECK(U(SetWindowContextHelpId)(w, 0x1357));
    ULONG_PTR lv = (ULONG_PTR)w;
    for (unsigned x = 1; x <= 8; x++) {
        WORD u = (WORD)(lv >> 16) ^ (WORD)(x * 0x0101);
        if (u == 0 || u == 0xffff || u == (WORD)(lv >> 16)) continue;
        HWND bad = (HWND)((lv & 0xffff) | ((ULONG_PTR)u << 16));
        SetLastError(SENTINEL_ERR);
        CHECKF(help_via(bad) == 0, "uniq 0x%x accepted", u);
        CHECKF(GetLastError() == ERR_INVALID_WINDOW_HANDLE, "uniq 0x%x error %lu", u, GetLastError());
    }
    CHECK(LO32(help_via(w)) == 0x1357);
    U(DestroyWindow)(w);
}

/* Reuse after destroy: the old handle keeps failing even if another window takes the slot. */
T(ranges_stale_handle_stays_invalid_after_slot_reuse) {
    NEED_RAW(NtUserCallHwnd);
    HWND a = k_make_window("r2ncm_a");
    if (!a) { t_skip("no window"); return; }
    U(DestroyWindow)(a);
    HWND b[8]; int nb = 0;
    for (int i = 0; i < 8; i++) { b[nb] = k_make_window("r2ncm_b"); if (b[nb]) nb++; }
    SetLastError(SENTINEL_ERR);
    CHECK(HW(a, 0x57) == 0);
    CHECK(GetLastError() == ERR_INVALID_WINDOW_HANDLE);
    for (int i = 0; i < nb; i++) { CHECK(b[i] != a); U(DestroyWindow)(b[i]); }
}

/* Cross-desktop: handle validation only accepts windows of the caller's desktop. */
struct xdesk { HDESK d; HWND w; HANDLE ready, stop; };
static DWORD WINAPI xdesk_proc(LPVOID p) {
    struct xdesk *x = (struct xdesk *)p;
    if (U(SetThreadDesktop)(x->d)) x->w = k_make_window("r2ncm_xdesk");
    SetEvent(x->ready);
    WaitForSingleObject(x->stop, 8000);
    if (x->w) U(DestroyWindow)(x->w);
    return 0;
}
T(ranges_window_on_other_desktop_is_invalid_handle) {
    NEED_RAW(NtUserCallHwnd);
    char name[64]; snprintf(name, sizeof name, "win32kt_r2ncm_%lu", (unsigned long)GetCurrentProcessId());
    HDESK d = U(CreateDesktopA)(name, NULL, NULL, 0, GENERIC_ALL, NULL);
    if (!d) { t_skip("CreateDesktop failed (%lu)", GetLastError()); return; }
    struct xdesk x = { d, NULL, CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL) };
    HANDLE th = CreateThread(NULL, 0, xdesk_proc, &x, 0, NULL);
    WaitForSingleObject(x.ready, 5000);
    if (x.w) {
        /* ValidateHwndEx: a window of another desktop is only accepted if the optional desktop-view
         * callback claims it; otherwise the handle is refused. */
        /* Windows 10: the refusal is silent, 0 comes back and the last error is left exactly as it was (no 1400). */
        SetLastError(SENTINEL_ERR);
        ULONG_PTR r = HW(x.w, 0x57);
        if (r == 0) CHECKF(GetLastError() == SENTINEL_ERR, "err %lu", GetLastError());
        SetLastError(SENTINEL_ERR);
        ULONG_PTR r2 = HWPLS(x.w, 0, 0x7d);
        if (r == 0) { CHECK(r2 == 0); CHECKF(GetLastError() == SENTINEL_ERR, "err %lu", GetLastError()); }
        else t_note("window of another desktop was accepted (desktop-view callback present)");
        if (r2) HWPLS(x.w, 1, 0x7d);
    } else t_skip("window on second desktop not created");
    SetEvent(x.stop);
    WaitForSingleObject(th, 5000); CloseHandle(th);
    CloseHandle(x.ready); CloseHandle(x.stop);
    U(CloseDesktop)(d);
}

/* A window of another thread on the same desktop is valid for the read-only context-help-id query. */
T(ranges_window_of_other_thread_same_desktop_is_valid) {
    NEED_RAW(NtUserCallHwnd);
    struct mx_bg bg;
    if (!mx_bg_start(&bg, 0x7777)) { t_skip("bg thread"); mx_bg_stop(&bg); return; }
    SetLastError(SENTINEL_ERR);
    CHECK(LO32(HW(bg.hwnd, 0x57)) == 0x7777);
    CHECKF(GetLastError() == SENTINEL_ERR, "err changed to %lu", GetLastError());
    mx_bg_stop(&bg);
}
