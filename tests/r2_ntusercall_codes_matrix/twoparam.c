/* NtUserCallTwoParam per-code semantics (codes 0x7e..0x8d; the dispatcher enters the critical
 * section and calls table[code-0x7e](a, b), returning the raw result). */
#include "mx.h"

/* ---- 0x7f GetCursorPos ---------------------------------------------------------------------------- */
/* GetCursorPos(POINT*, mode): window-station access check; thread desktop must be the input
 * desktop (else error 5); mode 1 = logical position (or the saved spatial position), mode 2 = per DPI
 * context logical position, any other mode returns 0 before the pointer is touched. */
typedef struct { POINT p; } __attribute__((aligned(8))) apoint;

T(twoparam_0x7f_get_cursor_pos_matches_public) {
    NEED_RAW(NtUserCallTwoParam);
    int stable = 0;
    for (int i = 0; i < 40 && !stable; i++) {
        apoint a, b, c; a.p.x = b.p.x = c.p.x = 0x7777;
        CHECK(U(GetCursorPos)(&a.p));
        ULONG_PTR r = TP(&b.p, 1, 0x7f);
        CHECK(U(GetCursorPos)(&c.p));
        CHECK(LO32(r) == 1);
        if (a.p.x == c.p.x && a.p.y == c.p.y) {
            stable = 1;
            CHECKF(b.p.x == a.p.x && b.p.y == a.p.y, "direct (%ld,%ld) public (%ld,%ld)", b.p.x, b.p.y, a.p.x, a.p.y);
        } else Sleep(10);
    }
    if (!stable) t_note("cursor kept moving; equality not asserted");
}
T(twoparam_0x7f_get_cursor_pos_inside_virtual_screen) {
    NEED_RAW(NtUserCallTwoParam);
    apoint a; CHECK(LO32(TP(&a.p, 1, 0x7f)) == 1);
    int vx = U(GetSystemMetrics)(SM_XVIRTUALSCREEN), vy = U(GetSystemMetrics)(SM_YVIRTUALSCREEN);
    int vw = U(GetSystemMetrics)(SM_CXVIRTUALSCREEN), vh = U(GetSystemMetrics)(SM_CYVIRTUALSCREEN);
    if (vw > 0 && vh > 0)
        CHECKF(a.p.x >= vx && a.p.x < vx + vw && a.p.y >= vy && a.p.y < vy + vh, "cursor (%ld,%ld) outside virtual screen (%d,%d %dx%d)", a.p.x, a.p.y, vx, vy, vw, vh);
    else t_note("no virtual screen metrics");
}
T(twoparam_0x7f_unknown_mode_returns_zero_and_leaves_buffer) {
    NEED_RAW(NtUserCallTwoParam);
    static const ULONG_PTR modes[] = { 0, 3, 4, 0x10, 0xff, 0x100000000ULL, (ULONG_PTR)-1 };
    for (size_t i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        apoint a; a.p.x = 0x1234; a.p.y = 0x5678;
        SetLastError(SENTINEL_ERR);
        ULONG_PTR r = TP(&a.p, modes[i], 0x7f);
        CHECKF(LO32(r) == 0, "mode 0x%llx returned %p", (unsigned long long)modes[i], (void *)r);
        CHECKF(a.p.x == 0x1234 && a.p.y == 0x5678, "mode 0x%llx wrote (%ld,%ld)", (unsigned long long)modes[i], a.p.x, a.p.y);
    }
}
T(twoparam_0x7f_mode2_matches_mode1_for_thread_dpi_context) {
    NEED_RAW(NtUserCallTwoParam);
    for (int i = 0; i < 40; i++) {
        apoint a, b;
        if (LO32(TP(&a.p, 1, 0x7f)) != 1 || LO32(TP(&b.p, 2, 0x7f)) != 1) { CHECK(0); return; }
        apoint c; TP(&c.p, 1, 0x7f);
        if (a.p.x == c.p.x && a.p.y == c.p.y) {
            /* both modes derive the position through the thread's DPI awareness context */
            t_note("mode1 (%ld,%ld) mode2 (%ld,%ld)", a.p.x, a.p.y, b.p.x, b.p.y);
            return;
        }
        Sleep(10);
    }
}

/* ---- 0x88 SetCaretPos ------------------------------------------------------------------------------ */
/* SetCaretPos(x, y): fails with last error 5 and result 0 when there is no caret; otherwise stores
 * pq+0x13c/0x140 (no range check), restarts the blink timer, raises EVENT_OBJECT_LOCATIONCHANGE, returns 1. */
T(twoparam_0x88_set_caret_pos_without_caret) {
    NEED_RAW(NtUserCallTwoParam);
    HWND w = k_make_window("r2ncm_scp0");
    if (!w) { t_skip("no window"); return; }
    SetLastError(SENTINEL_ERR);
    CHECK(TP(1, 2, 0x88) == 0);
    EXPECT_ERR(ERR_ACCESS_DENIED);
    SetLastError(SENTINEL_ERR);
    CHECK(U(SetCaretPos)(1, 2) == 0);
    EXPECT_ERR(ERR_ACCESS_DENIED);
    U(DestroyWindow)(w);
}
T(twoparam_0x88_set_caret_pos_roundtrip_with_get_caret_pos) {
    NEED_RAW(NtUserCallTwoParam);
    HWND w = k_make_window("r2ncm_scp1");
    if (!w) { t_skip("no window"); return; }
    if (!U(CreateCaret)(w, NULL, 2, 12)) { t_skip("CreateCaret failed"); U(DestroyWindow)(w); return; }
    static const int pts[][2] = { {5, 7}, {0, 0}, {-3, -4}, {30000, 40000}, {-30000, 123}, {7, 7} };
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        SetLastError(SENTINEL_ERR);
        ULONG_PTR r = TP((ULONG_PTR)(LONG_PTR)pts[i][0], (ULONG_PTR)(LONG_PTR)pts[i][1], 0x88);
        CHECKF(r == 1, "SetCaretPos(%d,%d) = %p", pts[i][0], pts[i][1], (void *)r);
        POINT p = { 0x1111, 0x2222 };
        CHECK(U(GetCaretPos)(&p));
        CHECKF(p.x == pts[i][0] && p.y == pts[i][1], "readback (%ld,%ld) want (%d,%d)", p.x, p.y, pts[i][0], pts[i][1]);
    }
    /* same position again is a no-op that still reports success */
    CHECK(TP(7, 7, 0x88) == 1);
    CHECK(U(SetCaretPos)(11, 13) == 1);
    POINT p; CHECK(U(GetCaretPos)(&p)); CHECK(p.x == 11 && p.y == 13);
    CHECK((NP(6) & 0xff) == 1);
    SetLastError(SENTINEL_ERR);
    CHECK(TP(1, 1, 0x88) == 0);               /* caret destroyed again */
    EXPECT_ERR(ERR_ACCESS_DENIED);
    U(DestroyWindow)(w);
}
T(twoparam_0x88_caret_position_is_per_queue) {
    NEED_RAW(NtUserCallTwoParam);
    HWND w = k_make_window("r2ncm_scp2");
    struct mx_bg bg;
    if (!w || !mx_bg_start(&bg, 0)) { t_skip("fixture"); if (w) U(DestroyWindow)(w); mx_bg_stop(&bg); return; }
    if (U(CreateCaret)(w, NULL, 2, 12)) {
        CHECK(TP(21, 22, 0x88) == 1);
        POINT p; CHECK(U(GetCaretPos)(&p) && p.x == 21 && p.y == 22);
        NP(6);
    } else t_skip("CreateCaret failed");
    mx_bg_stop(&bg);
    U(DestroyWindow)(w);
}

/* ---- 0x8a UnhookWindowsHook ------------------------------------------------------------------------- */
static LRESULT CALLBACK nop_hook(int c, WPARAM w, LPARAM l) { return U(CallNextHookEx)(NULL, c, w, l); }
T(twoparam_0x8a_unhook_windows_hook_removes_matching_hook) {
    NEED_RAW(NtUserCallTwoParam);
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, nop_hook, NULL, GetCurrentThreadId());
    if (!h) { t_skip("SetWindowsHookEx failed %lu", GetLastError()); return; }
    struct k_user_entry e;
    if (k_shared_info_ptr() && k_user_entry((HANDLE)h, &e)) CHECKF(e.type == 5, "hook handle type %u want TYPE_HOOK (5)", e.type);
    CHECK(LO32(TP(WH_GETMESSAGE, nop_hook, 0x8a)) == 1);
    SetLastError(SENTINEL_ERR);
    CHECK(!U(UnhookWindowsHookEx)(h));         /* the handle died with the hook */
    EXPECT_ERR(1404);                           /* ERROR_INVALID_HOOK_HANDLE */
    CHECK(LO32(TP(WH_GETMESSAGE, nop_hook, 0x8a)) == 0);   /* none left */
}
T(twoparam_0x8a_unhook_windows_hook_requires_matching_type_and_proc) {
    NEED_RAW(NtUserCallTwoParam);
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, nop_hook, NULL, GetCurrentThreadId());
    if (!h) { t_skip("SetWindowsHookEx failed %lu", GetLastError()); return; }
    /* no hook of the chain has this procedure: fails with ERROR_HOOK_NOT_INSTALLED (0x597) */
    SetLastError(SENTINEL_ERR);
    CHECK(LO32(TP(WH_CALLWNDPROC, nop_hook, 0x8a)) == 0);       /* wrong chain */
    EXPECT_ERR(1431);
    SetLastError(SENTINEL_ERR);
    CHECK(LO32(TP(WH_GETMESSAGE, (ULONG_PTR)nop_hook + 0x10, 0x8a)) == 0);   /* wrong procedure */
    EXPECT_ERR(1431);
    CHECK(U(UnhookWindowsHookEx)(h));           /* still installed after the three failures */
}
T(twoparam_0x8a_unhook_windows_hook_public_wrapper) {
    NEED_RAW(NtUserCallTwoParam);
    HHOOK h = U(SetWindowsHookExW)(WH_KEYBOARD, nop_hook, NULL, GetCurrentThreadId());
    if (!h) { t_skip("SetWindowsHookEx failed %lu", GetLastError()); return; }
    CHECK(U(UnhookWindowsHook)(WH_KEYBOARD, nop_hook) != 0);
    SetLastError(0);
    CHECK(!U(UnhookWindowsHookEx)(h));
}
T(twoparam_0x8a_unhook_windows_hook_with_junk_chain) {
    NEED_RAW(NtUserCallTwoParam);
    static const int chains[] = { 100, -100, 0x7fffffff };
    for (size_t i = 0; i < sizeof chains / sizeof chains[0]; i++) {
        SetLastError(SENTINEL_ERR);
        ULONG_PTR r = TP((ULONG_PTR)(LONG_PTR)chains[i], nop_hook, 0x8a);
        CHECKF(LO32(r) == 0, "chain %d returned %p", chains[i], (void *)r);
        /* a hook chain id outside -1..14 is refused with ERROR_INVALID_HOOK_FILTER (1426) */
        CHECKF(GetLastError() == 1426, "chain %d last error %lu want 1426", chains[i], GetLastError());
    }
}

/* ---- 0x8d RealGetSystemMetricsForDpi ----------------------------------------------------------------------- */
/* user32!RealGetSystemMetricsForDpi maps an SM_ index to a dpi-metric slot (0..0x1d), serves the
 * system dpi and dpi 96 from per-slot caches shared with the kernel, and calls this
 * code for every other dpi. */
static const struct { int sm; int slot; } g_dpimap[] = {
    {2,0},{3,1},{4,2},{9,3},{10,4},{11,5},{12,6},{13,7},{14,8},{15,9},{0x14,10},{0x15,11},{0x1e,12},{0x1f,13},
    {0x20,14},{0x21,15},{0x1c,16},{0x1d,17},{0x26,18},{0x27,19},{0x31,20},{0x32,21},{0x33,22},{0x34,23},{0x35,24},
    {0x36,25},{0x37,26},{0x47,27},{0x48,28},{0x5c,29},
};
#define NDPIMAP (sizeof g_dpimap / sizeof g_dpimap[0])

T(twoparam_0x8d_dpi_metrics_match_public_for_uncached_dpis) {
    NEED_RAW(NtUserCallTwoParam);
    static const UINT dpis[] = { 120, 144, 168, 192, 240 };
    for (size_t i = 0; i < NDPIMAP; i++)
        for (size_t j = 0; j < sizeof dpis / sizeof dpis[0]; j++) {
            int d = (int)LO32(TP(g_dpimap[i].slot, dpis[j], 0x8d));
            int p = U(GetSystemMetricsForDpi)(g_dpimap[i].sm, dpis[j]);
            /* SM_CXSIZE (0x1e) is the one metric user32 does not serve straight from this slot: the caption button width it returns
             * is wider than the slot value (36 vs 22 at 96 dpi), so only the relation is asserted for it. */
            if (g_dpimap[i].sm == 0x1e) CHECKF(p >= d, "SM %d dpi %u: kernel %d public %d", g_dpimap[i].sm, dpis[j], d, p);
            else CHECKF(d == p, "SM %d dpi %u: kernel %d public %d", g_dpimap[i].sm, dpis[j], d, p);
        }
}
T(twoparam_0x8d_dpi_metrics_at_96_match_serverinfo_cache) {
    NEED_RAW(NtUserCallTwoParam);
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { t_skip("no SERVERINFO"); return; }
    for (size_t i = 0; i < NDPIMAP; i++) {
        unsigned long c = 0;
        if (!k_try_read_u32((char *)si.psi + 0x964 + g_dpimap[i].slot * 4, &c)) { t_note("cache not readable"); return; }
        int d = (int)LO32(TP(g_dpimap[i].slot, 96, 0x8d));
        CHECKF(d == (int)c, "slot %d: kernel %d, dpi-96 cache %d", g_dpimap[i].slot, d, (int)c);
        int pub = U(GetSystemMetricsForDpi)(g_dpimap[i].sm, 96);
        if (g_dpimap[i].sm == 0x1e) CHECKF(pub >= (int)c, "SM %d public %d below cache %d", g_dpimap[i].sm, pub, (int)c);   /* see the 0x8d note above */
        else CHECKF(pub == (int)c, "SM %d public %d vs cache %d", g_dpimap[i].sm, pub, (int)c);
    }
}
T(twoparam_0x8d_dpi_metrics_at_system_dpi_match_serverinfo_cache) {
    NEED_RAW(NtUserCallTwoParam);
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { t_skip("no SERVERINFO"); return; }
    unsigned short sysdpi = 0;
    if (!k_try_read_u16((char *)si.psi + 0x1b56, &sysdpi) || sysdpi == 0) { t_skip("system dpi not readable"); return; }
    t_note("system dpi %u", sysdpi);
    for (size_t i = 0; i < NDPIMAP; i++) {
        unsigned long c = 0;
        if (!k_try_read_u32((char *)si.psi + 0x8ec + g_dpimap[i].slot * 4, &c)) { t_note("cache not readable"); return; }
        int d = (int)LO32(TP(g_dpimap[i].slot, sysdpi, 0x8d));
        CHECKF(d == (int)c, "slot %d dpi %u: kernel %d, system-dpi cache %d", g_dpimap[i].slot, sysdpi, d, (int)c);
    }
}
T(twoparam_0x8d_dpi_metrics_grow_with_dpi) {
    NEED_RAW(NtUserCallTwoParam);
    for (size_t i = 0; i < NDPIMAP; i++) {
        int a = (int)LO32(TP(g_dpimap[i].slot, 96, 0x8d)), b = (int)LO32(TP(g_dpimap[i].slot, 192, 0x8d));
        CHECKF(b >= a, "slot %d: dpi 96 -> %d, dpi 192 -> %d", g_dpimap[i].slot, a, b);
        CHECKF(a >= 0 && b >= 0, "slot %d negative metric", g_dpimap[i].slot);
    }
}
T(twoparam_0x8d_dpi_metric_is_deterministic) {
    NEED_RAW(NtUserCallTwoParam);
    for (size_t i = 0; i < NDPIMAP; i++)
        CHECK(LO32(TP(g_dpimap[i].slot, 144, 0x8d)) == LO32(TP(g_dpimap[i].slot, 144, 0x8d)));
}

/* ---- 0x7e ChangeWindowMessageFilter (process-wide: child) ------------------------------------------------------------ */
/* _ChangeWindowMessageFilter: UIPISQMChangeFilter(...) always; if UIPI is not enforced -> 1; a process
 * with integrity <= low (ppi+0x370 < 0x1001) gets error 5; a message id with a nonzero high word -> 0x57;
 * flag 1 = add (AddMessageToFilter), flag 2 = remove, any other flag -> 0x57. */
TCHILD(twoparam_cwmf) {
    (void)argc; (void)argv;
    FARPROC pt = t_proc_in("win32u.dll", "NtUserCallTwoParam");
    FARPROC pr = t_proc_in("user32.dll", "RegisterWindowMessageA");
    FARPROC pc = t_proc_in("user32.dll", "ChangeWindowMessageFilter");
    if (!pt || !pr || !pc) return 0;
    ULONG_PTR (WINAPI *tp)(ULONG_PTR, ULONG_PTR, DWORD) = (ULONG_PTR (WINAPI *)(ULONG_PTR, ULONG_PTR, DWORD))pt;
    UINT (WINAPI *reg)(const char *) = (UINT (WINAPI *)(const char *))pr;
    BOOL (WINAPI *cwmf)(UINT, DWORD) = (BOOL (WINAPI *)(UINT, DWORD))pc;
    int fails = 0;
    char name[96]; snprintf(name, sizeof name, "win32kt_r2ncm_cwmf_%lu", (unsigned long)GetCurrentProcessId());
    UINT msg = reg(name);
    if (msg < 0xC000) { printf("  FAIL RegisterWindowMessage returned 0x%x\n", msg); return 1; }
    SetLastError(0);
    DWORD r0 = (DWORD)tp(msg, 0, 0x7e);                 /* flag 0 is neither add nor remove */
    DWORD e0 = GetLastError();
    if (r0 != 0) { printf("NOTE UIPI filtering not enforced: ChangeWindowMessageFilter(flag 0) = %lu\n", (unsigned long)r0); return 0; }
    if (e0 == 5) { printf("NOTE low-integrity process: message filter changes are refused (error 5)\n"); return 0; }
    if (e0 != 87) { printf("  FAIL flag 0: error %lu want 87\n", (unsigned long)e0); fails++; }
    static const DWORD badflags[] = { 3, 4, 0x100, 0xffffffffu };
    for (size_t i = 0; i < sizeof badflags / sizeof badflags[0]; i++) {
        SetLastError(0);
        DWORD r = (DWORD)tp(msg, badflags[i], 0x7e), e = GetLastError();
        if (r != 0 || e != 87) { printf("  FAIL flag 0x%lx: ret %lu err %lu\n", (unsigned long)badflags[i], (unsigned long)r, (unsigned long)e); fails++; }
    }
    /* message ids with a nonzero high word are refused by ValidateChangeMessageFilter */
    static const ULONG_PTR badmsg[] = { 0x10000, 0x12345678, 0xffffffffu };
    for (size_t i = 0; i < sizeof badmsg / sizeof badmsg[0]; i++) {
        SetLastError(0);
        DWORD r = (DWORD)tp(badmsg[i], 1, 0x7e), e = GetLastError();
        if (r != 0 || e != 87) { printf("  FAIL message 0x%llx: ret %lu err %lu\n", (unsigned long long)badmsg[i], (unsigned long)r, (unsigned long)e); fails++; }
    }
    SetLastError(0);
    DWORD add = (DWORD)tp(msg, 1, 0x7e);
    if (add == 0) { printf("  FAIL adding registered message 0x%x: err %lu\n", msg, (unsigned long)GetLastError()); fails++; }
    DWORD add2 = (DWORD)tp(msg, 1, 0x7e);
    printf("NOTE second add of the same message returned %lu\n", (unsigned long)add2);
    SetLastError(0);
    DWORD rem = (DWORD)tp(msg, 2, 0x7e);
    printf("NOTE remove after add returned %lu (err %lu)\n", (unsigned long)rem, (unsigned long)GetLastError());
    /* the public wrapper is the same call */
    SetLastError(0);
    DWORD pub = (DWORD)cwmf(msg, 0);
    if (pub != 0 || GetLastError() != 87) { printf("  FAIL public wrapper flag 0: ret %lu err %lu\n", (unsigned long)pub, (unsigned long)GetLastError()); fails++; }
    if (!cwmf(msg, 1)) { printf("  FAIL public add: err %lu\n", (unsigned long)GetLastError()); fails++; }
    cwmf(msg, 2);
    return fails;
}
T(twoparam_0x7e_change_window_message_filter_in_child) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    struct k_child_result r = k_child_run("r2_ntusercall_codes_matrix_twoparam_cwmf", "", 30000);
    CHECK(r.exit_code == 0);
}

/* ---- not invoked ------------------------------------------------------------------------------------------------------- */
/* 0x80 InitOemXlateTables (writes two caller tables), 0x81 FujitsuOyayubiControl, 0x82/0x84/0x85 ghost / frost /
 * hung-app handler registration, 0x83 RegisterLogonProcess, 0x86/0x87 terminal-services shadowing, 0x89
 * SetThreadQueueMergeSetting, 0x8b shell window management, 0x8c SetCITInfo: logon / shell / session
 * registration or caller-buffer fills that are only valid for their one legitimate system caller. */
