/* gui_thread_conversion / user32path: the same conversion, reached through the normal user32 /
 * gdi32 path, plus the process connection that loading user32.dll performs.
 *
 * Facts from the analysis (19041):
 *  - user32!_UserClientDllInitialize (DLL_PROCESS_ATTACH) connects to csrss, fills gSharedInfo and
 *    stores &apfnDispatch (or a heap copy of it for system-call-filter mitigation) into PEB+0x58,
 *    the kernel callback table. Slot 0x54 is user32!__ClientThreadSetup (win32kfull!
 *    the kernel performs a user-mode callback (0x54)).
 *  - user32!IsGUIThread(bConvert): TEB+0x78 != 0 -> TRUE; otherwise FALSE for bConvert == 0, else
 *    NtUserGetThreadState(0xE) (which converts) and its result.
 *  - user32!GetKeyState starts with the same TEB+0x78 test and the same NtUserGetThreadState(0xE).
 *  - user32!ClientThreadSetup: returns at once when CLIENTINFO.flags & 8; otherwise sets that bit,
 *    allocates the local DESKTOPINFO (0x48 bytes) when CLIENTINFO+0x20 is still empty, and finally
 *    stores &gdwRegisteredClasses at CLIENTINFO+0xd0 (so +0xd0 points into user32's data).
 *  - user32!IsWindow: CLIENTINFO+0x20 -> desktop-base guard word, +0x28 = ulClientDelta (client
 *    pointer = ulClientDelta + handle-entry delta), +0x40/+0x48 = last validated window cache. */
#include "gtc.h"

static HMODULE load_user32(void) { return t_module("user32.dll"); }

/* ---------------------------------------------------------------------------------------- */
/* Process connection by loading user32.dll                                                 */
/* ---------------------------------------------------------------------------------------- */

SCEN(user32_load_sets_callback_table) {
    gtc_assert_pristine();
    GEQ(gtc_peb_u64(PEB_KernelCallbackTable), 0);
    HMODULE m = load_user32();
    GC(m != NULL);
    unsigned long long tbl = gtc_peb_u64(PEB_KernelCallbackTable);
    GCF(tbl != 0, "PEB.KernelCallbackTable still NULL after user32 DllMain");
    GC(!gtc_is_kernel_range(tbl));
    unsigned long long q; GC(k_try_read((void *)(ULONG_PTR)tbl, &q, 8));
    /* UserSharedInfoPtr is the same PEB slot (anonymous union in _PEB) */
    GEQ(gtc_peb_u64(PEB_UserSharedInfoPtr), tbl);
    t_note("KernelCallbackTable=%p user32 base=%p in-image=%d", (void *)(ULONG_PTR)tbl, (void *)m, gtc_in_module("user32.dll", tbl));
}

SCEN(user32_load_callback_table_location) {
    HMODULE m = load_user32();
    GC(m != NULL);
    unsigned long long tbl = gtc_peb_u64(PEB_KernelCallbackTable);
    if (!tbl) { GC(tbl != 0); return; }
    /* apfnDispatch lives in user32's data; the system-call-filter variant is a RtlAllocateHeap copy. Either way: user-mode. */
    int in_img = gtc_in_module("user32.dll", tbl);
    DWORD type = 0; SIZE_T reg = 0;
    DWORD prot = gtc_query((void *)(ULONG_PTR)tbl, &type, &reg);
    GC(prot != 0);
    GC(in_img || type == MEM_PRIVATE);
    t_note("table in user32 image: %d, protection 0x%lx, type 0x%lx", in_img, prot, type);
}

SCEN(user32_load_callback_slot_clientthreadsetup) {
    load_user32();
    unsigned n = k_cb_count();
    t_note("%u consecutive non-NULL callback slots", n);
    GCF(n > KCB_ClientThreadSetup, "count %u does not reach slot 0x54", n);
    unsigned long long tbl = gtc_peb_u64(PEB_KernelCallbackTable), fn = 0;
    if (!tbl || !k_try_read((char *)(ULONG_PTR)tbl + KCB_ClientThreadSetup * 8, &fn, 8)) { GC(0); return; }
    GC(fn != 0);
    GC(gtc_in_module("user32.dll", fn));      /* __ClientThreadSetup */
    unsigned long long exp = (unsigned long long)(ULONG_PTR)GetProcAddress(GetModuleHandleA("user32.dll"), "ClientThreadSetup");
    t_note("slot 0x54=%p export ClientThreadSetup=%p", (void *)(ULONG_PTR)fn, (void *)(ULONG_PTR)exp);
}

SCEN(user32_load_callback_slots_inside_user32) {
    load_user32();
    unsigned long long tbl = gtc_peb_u64(PEB_KernelCallbackTable);
    GC(tbl != 0);
    int inside = 0, outside = 0, nul = 0;
    for (int i = 0; i < 0x7B; i++) {
        unsigned long long fn = 0;
        if (!k_try_read((char *)(ULONG_PTR)tbl + i * 8, &fn, 8)) { GCF(0, "slot %d unreadable", i); continue; }
        if (!fn) nul++; else if (gtc_in_module("user32.dll", fn)) inside++; else { outside++; t_note("slot 0x%x -> %p outside user32", i, (void *)(ULONG_PTR)fn); }
    }
    t_note("slots: %d inside user32, %d outside, %d NULL", inside, outside, nul);
    GC(inside > 0x20);
}

SCEN(user32_load_shared_info_published) {
    load_user32();
    struct k_user_shared_info si;
    if (!k_shared_info(&si)) { t_skip("gSharedInfo unreadable"); return; }
    /* user32 copies the csrss-provided SHAREDINFO into gSharedInfo during DllMain */
    GC(si.psi != NULL);
    GC(si.aheList != NULL);
    GC(!gtc_is_kernel_range((unsigned long long)(ULONG_PTR)si.psi));
    unsigned long long cHandleEntries = 0;
    if (k_try_read((char *)si.psi + 8, &cHandleEntries, 8)) {
        /* IsWindow */
        GC(cHandleEntries > 0);
        GC(cHandleEntries <= 0x10000);
    } else GC(0);
    t_note("psi=%p aheList=%p heEntrySize=%lu cHandleEntries=%llu", si.psi, si.aheList, si.heEntrySize, cHandleEntries);
}

SCEN(user32_load_converts_loading_thread) {
    gtc_assert_pristine();
    load_user32();
    /* user32!_UserClientDllInitialize calls gdi32full!GdiDllInitialize(DLL_PROCESS_ATTACH), which calls
     * NtGdiInit (service number 0x12e2 >= 0x1000) on the loading thread: the first win32k syscall
     * converts that thread, so the main thread is already a GUI thread when LoadLibrary returns. */
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), GetCurrentThreadId());
    GNE(gtc_peb_u64(PEB_GdiSharedHandleTable), 0);
    GEQ(gtc_peb_u32(PEB_GdiDCAttributeList_off), 0x14);
    GNE(gtc_ci_u32(CI_dwExpWinVer), 0);
    t_note("after LoadLibrary(user32): CI_flags=0x%llx", gtc_ci_u64(CI_flags_off));
}

SCEN(user32_load_isguithread_false_true_on_loader) {
    gtc_assert_pristine();
    HMODULE m = load_user32();
    BOOL (WINAPI *igt)(BOOL) = (BOOL (WINAPI *)(BOOL))GetProcAddress(m, "IsGUIThread");
    if (!igt) { t_skip("no IsGUIThread"); return; }
    /* user32!IsGUIThread(FALSE) is just `TEB+0x78 != 0`; the loader thread was converted by NtGdiInit */
    GEQ(igt(FALSE), 1);
}

SCEN(user32_load_gdi_table_readonly_view) {
    gtc_assert_pristine();
    load_user32();
    unsigned long long t = gtc_peb_u64(PEB_GdiSharedHandleTable);
    GC(t != 0);
    if (!t) return;
    DWORD type = 0; SIZE_T reg = 0;
    /* GdiProcessCallout maps the shared handle section PAGE_READONLY; gdi32 only reads it */
    GEQ(gtc_query((void *)(ULONG_PTR)t, &type, &reg), PAGE_READONLY);
    GEQ(type, MEM_MAPPED);
    GEQ(t & 0xFFFF, 0);
}

SCEN(gdi32_stock_object_cells_match_handles) {
    HMODULE g = t_module("gdi32.dll");
    if (!g) { t_skip("no gdi32"); return; }
    HGDIOBJ (WINAPI *gso)(int) = (HGDIOBJ (WINAPI *)(int))GetProcAddress(g, "GetStockObject");
    DWORD (WINAPI *got)(HGDIOBJ) = (DWORD (WINAPI *)(HGDIOBJ))GetProcAddress(g, "GetObjectType");
    if (!gso || !got) { t_skip("missing exports"); return; }
    static const struct { int id; DWORD type; } so[] = {
        { WHITE_BRUSH, OBJ_BRUSH }, { BLACK_BRUSH, OBJ_BRUSH }, { DC_BRUSH, OBJ_BRUSH },
        { BLACK_PEN, OBJ_PEN }, { DC_PEN, OBJ_PEN }, { SYSTEM_FONT, OBJ_FONT }, { DEFAULT_PALETTE, OBJ_PAL } };
    for (size_t i = 0; i < sizeof so / sizeof so[0]; i++) {
        HGDIOBJ h = gso(so[i].id);
        GCF(h != NULL, "stock %d", so[i].id);
        if (!h) continue;
        GEQ(got(h), so[i].type);
        /* stock handles live in the same shared table: the cell's check byte is the handle's top byte */
        struct k_gdicell c;
        if (k_gdi_cell(h, &c)) GEQ(c.type_check, (unsigned char)((ULONG_PTR)h >> 24));
        GC(((ULONG_PTR)h & 0xFFFF) != 0);
    }
}

static void w_ci_after_user32(void *p) {
    unsigned long long *r = (unsigned long long *)p;
    BOOL (WINAPI *igt)(BOOL) = (BOOL (WINAPI *)(BOOL))GetProcAddress(GetModuleHandleA("user32.dll"), "IsGUIThread");
    igt(TRUE);
    r[0] = gtc_teb_u64(TEB_Win32ThreadInfo);
    r[1] = GetCurrentThreadId();
    r[2] = gtc_ci_u64(CI_hKL);
    r[3] = gtc_ci_u64(CI_flags_off);
    r[4] = gtc_ci_u64(CI_pRegClasses);
    r[5] = gtc_peb_u64(PEB_KernelCallbackTable);
    r[6] = gtc_ci_u32(CI_dwExpWinVer);
}
SCEN(worker_after_user32_shares_process_state) {
    load_user32();
    unsigned long long r[8]; memset(r, 0, sizeof r);
    GC(gtc_thread(w_ci_after_user32, r));
    GEQ(r[0], r[1]);
    GEQ(r[5], gtc_peb_u64(PEB_KernelCallbackTable));     /* one table per process */
    GEQ(r[2], gtc_ci_u64(CI_hKL));                       /* default layout identical for both threads */
    GEQ(r[6], gtc_ci_u32(CI_dwExpWinVer));               /* process-level ExpWinVer */
    /* ClientThreadSetup (callback 0x54), when it ran for the worker, ends by pointing +0xd0 into user32 */
    if (r[3] & 8) GC(gtc_in_module("user32.dll", r[4]));
    else GEQ(r[4], 0);
}

SCEN(user32_load_pulls_gdi32) {
    load_user32();
    /* user32 imports gdi32 (and gdi32full through it) */
    GC(GetModuleHandleA("gdi32.dll") != NULL);
    GC(GetModuleHandleA("win32u.dll") != NULL);
}

SCEN(user32_load_twice_keeps_table) {
    load_user32();
    unsigned long long a = gtc_peb_u64(PEB_KernelCallbackTable);
    HMODULE m2 = LoadLibraryA("user32.dll");
    GC(m2 != NULL);
    GEQ(gtc_peb_u64(PEB_KernelCallbackTable), a);
    FreeLibrary(m2);
    GEQ(gtc_peb_u64(PEB_KernelCallbackTable), a);
}

SCEN(user32_first_gui_call_main_thread_converts) {
    gtc_assert_pristine();
    load_user32();
    BOOL (WINAPI *igt)(BOOL) = (BOOL (WINAPI *)(BOOL))GetProcAddress(GetModuleHandleA("user32.dll"), "IsGUIThread");
    GC(igt != NULL);
    if (!igt) return;
    GEQ(igt(TRUE), 1);
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), GetCurrentThreadId());
    GEQ(igt(FALSE), 1);
    GNE(gtc_peb_u64(PEB_GdiSharedHandleTable), 0);
    GEQ(gtc_peb_u32(PEB_GdiDCAttributeList_off), 0x14);
}

/* ---------------------------------------------------------------------------------------- */
/* IsGUIThread semantics on a fresh worker thread                                           */
/* ---------------------------------------------------------------------------------------- */

typedef BOOL (WINAPI *igt_t)(BOOL);
static igt_t g_igt;
struct r1 { unsigned long long a[16]; };

static void w_igt_false(void *p) {
    struct r1 *r = (struct r1 *)p;
    r->a[0] = gtc_teb_u64(TEB_Win32ThreadInfo);
    r->a[1] = g_igt(FALSE);
    r->a[2] = gtc_teb_u64(TEB_Win32ThreadInfo);
    r->a[3] = g_igt(FALSE);
    r->a[4] = g_igt(FALSE);
    r->a[5] = gtc_teb_u64(TEB_Win32ThreadInfo);
    r->a[6] = gtc_ci_all_zero();
}
SCEN(isguithread_false_never_converts) {
    load_user32();
    g_igt = (igt_t)GetProcAddress(GetModuleHandleA("user32.dll"), "IsGUIThread");
    if (!g_igt) { t_skip("no IsGUIThread"); return; }
    struct r1 r; memset(&r, 0, sizeof r);
    GC(gtc_thread(w_igt_false, &r));
    GEQ(r.a[0], 0); GEQ(r.a[1], 0); GEQ(r.a[2], 0); GEQ(r.a[3], 0); GEQ(r.a[4], 0); GEQ(r.a[5], 0);
    GEQ(r.a[6], 1);
}

static void w_igt_true(void *p) {
    struct r1 *r = (struct r1 *)p;
    r->a[0] = gtc_teb_u64(TEB_Win32ThreadInfo);
    r->a[1] = g_igt(FALSE);
    r->a[2] = g_igt(TRUE);
    r->a[3] = gtc_teb_u64(TEB_Win32ThreadInfo);
    r->a[4] = g_igt(FALSE);
    r->a[5] = g_igt(TRUE);
    r->a[6] = GetCurrentThreadId();
}
SCEN(isguithread_true_converts_then_false_reports_true) {
    load_user32();
    g_igt = (igt_t)GetProcAddress(GetModuleHandleA("user32.dll"), "IsGUIThread");
    if (!g_igt) { t_skip("no IsGUIThread"); return; }
    struct r1 r; memset(&r, 0, sizeof r);
    GC(gtc_thread(w_igt_true, &r));
    GEQ(r.a[0], 0);
    GEQ(r.a[1], 0);
    GEQ(r.a[2], 1);            /* NtUserGetThreadState(0xE) == 1 */
    GEQ(r.a[3], r.a[6]);
    GEQ(r.a[4], 1);            /* TEB+0x78 != 0 -> TRUE without a syscall */
    GEQ(r.a[5], 1);
}

static void w_igt_nonzero(void *p) {
    struct r1 *r = (struct r1 *)p;
    r->a[0] = g_igt((BOOL)0x12345678);
    r->a[1] = gtc_teb_u64(TEB_Win32ThreadInfo);
    r->a[2] = GetCurrentThreadId();
}
SCEN(isguithread_any_nonzero_convert_arg) {
    load_user32();
    g_igt = (igt_t)GetProcAddress(GetModuleHandleA("user32.dll"), "IsGUIThread");
    if (!g_igt) { t_skip("no IsGUIThread"); return; }
    struct r1 r; memset(&r, 0, sizeof r);
    GC(gtc_thread(w_igt_nonzero, &r));
    /*: the test is on the whole 32-bit value */
    GEQ(r.a[0], 1);
    GEQ(r.a[1], r.a[2]);
}

/* ---------------------------------------------------------------------------------------- */
/* Which user32 / gdi32 entry points convert a fresh thread                                  */
/* ---------------------------------------------------------------------------------------- */

static int g_trig;
static HMODULE g_u32;
static void trig_run(int t) {
    FARPROC p;
    switch (t) {
    case 0: ((BOOL (WINAPI *)(BOOL))GetProcAddress(g_u32, "IsGUIThread"))(TRUE); break;
    case 1: ((SHORT (WINAPI *)(int))GetProcAddress(g_u32, "GetKeyState"))(VK_SHIFT); break;
    case 2: ((HKL (WINAPI *)(DWORD))GetProcAddress(g_u32, "GetKeyboardLayout"))(0); break;
    case 3: ((BOOL (WINAPI *)(HWND))GetProcAddress(g_u32, "IsHungAppWindow"))(NULL); break;
    case 4: ((HDESK (WINAPI *)(DWORD))GetProcAddress(g_u32, "GetThreadDesktop"))(GetCurrentThreadId()); break;
    case 5: ((BOOL (WINAPI *)(void))GetProcAddress(GetModuleHandleA("gdi32.dll"), "GdiFlush"))(); break;
    case 6: ((HWND (WINAPI *)(void))GetProcAddress(g_u32, "GetForegroundWindow"))(); break;
    case 7: {
        HDC (WINAPI *ccd)(HDC) = (HDC (WINAPI *)(HDC))GetProcAddress(GetModuleHandleA("gdi32.dll"), "CreateCompatibleDC");
        BOOL (WINAPI *ddc)(HDC) = (BOOL (WINAPI *)(HDC))GetProcAddress(GetModuleHandleA("gdi32.dll"), "DeleteDC");
        HDC dc = ccd(NULL); if (dc) ddc(dc);
        break; }
    case 8: {
        HDC (WINAPI *gdc)(HWND) = (HDC (WINAPI *)(HWND))GetProcAddress(g_u32, "GetDC");
        int (WINAPI *rdc)(HWND, HDC) = (int (WINAPI *)(HWND, HDC))GetProcAddress(g_u32, "ReleaseDC");
        HDC dc = gdc(NULL); if (dc) rdc(NULL, dc);
        break; }
    case 9: {
        HBITMAP (WINAPI *cb)(int, int, UINT, UINT, const void *) = (HBITMAP (WINAPI *)(int, int, UINT, UINT, const void *))GetProcAddress(GetModuleHandleA("gdi32.dll"), "CreateBitmap");
        BOOL (WINAPI *dob)(HGDIOBJ) = (BOOL (WINAPI *)(HGDIOBJ))GetProcAddress(GetModuleHandleA("gdi32.dll"), "DeleteObject");
        HBITMAP b = cb(1, 1, 1, 1, NULL); if (b) dob(b);
        break; }
    }
    (void)p;
}

struct r2 { unsigned long long before, after, tid, igt_after; int ci_ok_flags; unsigned long long ci_flags, rc, exp, di; };
static void ck_ci_converted(const struct r2 *r) {
    /* InitClientInfo always runs at conversion */
    GCF(r->exp != 0, "dwExpWinVer is 0");
    if (r->ci_flags & 8) {
        /* ClientThreadSetup completed: its last act is *(CLIENTINFO+0xd0) = &gdwRegisteredClasses */
        GC(gtc_in_module("user32.dll", r->rc));
        GC(r->di != 0);
    } else {
        GEQ(r->rc, 0);
    }
}
static void w_trigger(void *p) {
    struct r2 *r = (struct r2 *)p;
    r->tid = GetCurrentThreadId();
    r->before = gtc_teb_u64(TEB_Win32ThreadInfo);
    trig_run(g_trig);
    r->after = gtc_teb_u64(TEB_Win32ThreadInfo);
    r->igt_after = g_igt(FALSE);
    r->ci_flags = gtc_ci_u64(CI_flags_off);
    r->rc = gtc_ci_u64(CI_pRegClasses);
    r->exp = gtc_ci_u32(CI_dwExpWinVer);
    r->di = gtc_ci_u64(CI_pDeskInfo);
}
SCEN_BODY(trigger) {
    g_trig = atoi(gtc_arg());
    g_u32 = load_user32();
    g_igt = (igt_t)GetProcAddress(g_u32, "IsGUIThread");
    if (!g_igt || !GetModuleHandleA("gdi32.dll")) { t_skip("missing user32/gdi32"); return; }
    struct r2 r; memset(&r, 0, sizeof r);
    GC(gtc_thread(w_trigger, &r));
    GEQ(r.before, 0);
    GCF(r.after != 0, "trigger %d did not convert", g_trig);
    GEQ(r.after, r.tid);
    GEQ(r.igt_after, 1);
    ck_ci_converted(&r);
    t_note("trigger %d: CI_flags=0x%llx dwExpWinVer=0x%llx", g_trig, r.ci_flags, r.exp);
}
SCEN_ARG(trigger_isguithread_true, trigger, "0")
SCEN_ARG(trigger_getkeystate, trigger, "1")
SCEN_ARG(trigger_getkeyboardlayout, trigger, "2")
SCEN_ARG(trigger_ishungappwindow, trigger, "3")
SCEN_ARG(trigger_getthreaddesktop, trigger, "4")
SCEN_ARG(trigger_gdiflush, trigger, "5")
SCEN_ARG(trigger_getforegroundwindow, trigger, "6")
SCEN_ARG(trigger_createcompatibledc, trigger, "7")
SCEN_ARG(trigger_getdc, trigger, "8")
SCEN_ARG(trigger_createbitmap, trigger, "9")

/* ---------------------------------------------------------------------------------------- */
/* Windows / desktop attach visible in CLIENTINFO                                           */
/* ---------------------------------------------------------------------------------------- */

struct r3 { HWND hw; unsigned long long ti, di, delta, tid, cache_h; int entry_ok, ptr_ok; struct k_user_entry e; void *wp; unsigned long long head_h; unsigned short w2a; int isw; DWORD thr; HDESK desk; };
static void w_window(void *p) {
    struct r3 *r = (struct r3 *)p;
    r->tid = GetCurrentThreadId();
    r->hw = k_make_window("gtc_win");
    r->ti = gtc_teb_u64(TEB_Win32ThreadInfo);
    r->di = gtc_ci_u64(CI_pDeskInfo);
    r->delta = gtc_ci_u64(CI_ulClientDelta);
    if (r->hw) {
        r->isw = U(IsWindow)(r->hw);
        r->entry_ok = k_user_entry(r->hw, &r->e);
        r->wp = k_wnd_ptr(r->hw);
        if (r->wp) {
            k_try_read(r->wp, &r->head_h, 8);
            k_try_read((char *)r->wp + 0x2a, &r->w2a, 2);
        }
        r->thr = U(GetWindowThreadProcessId)(r->hw, NULL);
        U(DestroyWindow)(r->hw);
    }
    r->cache_h = gtc_ci_u64(CI_hWndCache);
    r->desk = U(GetThreadDesktop)(r->tid);
}
SCEN(window_creation_publishes_desktop_state_in_clientinfo) {
    load_user32();
    struct r3 r; memset(&r, 0, sizeof r);
    GC(gtc_thread(w_window, &r));
    if (!r.hw) { t_skip("cannot create window (no desktop?)"); return; }
    GEQ(r.ti, r.tid);                              /* creating a window converted the thread */
    GC(r.isw);
    GEQ(r.thr, r.tid);                             /* NtUserQueryWindow(QUERY_WINDOW_UNIQUE_THREAD_ID) */
    GC(r.di != 0);
    GC(r.desk != NULL);
    if (r.entry_ok) {
        GEQ(r.e.type, 1);                          /* TYPE_WINDOW (IsWindow tests byte +0x18 == 1) */
        GEQ(r.e.flags & 1, 0);
        GEQ(r.e.uniq, (unsigned)(((ULONG_PTR)r.hw >> 16) & 0xFFFF));
    }
    if (r.wp) {
        /* tagWND begins with the HEAD: the handle */
        GEQ(r.head_h, (ULONG_PTR)r.hw);
        GC((r.w2a & 0x8000) == 0);                 /* IsWindow: negative word at +0x2a means destroyed */
    }
    t_note("pDeskInfo=%p ulClientDelta=0x%llx wnd=%p", (void *)(ULONG_PTR)r.di, r.delta, r.wp);
}

struct r4 { HWND hw[3]; DWORD thr[3]; int isw[3]; };
static void w_wins(void *p) {
    struct r4 *r = (struct r4 *)p;
    for (int i = 0; i < 3; i++) {
        r->hw[i] = k_make_window("gtc_multi");
        if (r->hw[i]) { r->thr[i] = U(GetWindowThreadProcessId)(r->hw[i], NULL); r->isw[i] = U(IsWindow)(r->hw[i]); }
    }
    for (int i = 0; i < 3; i++) if (r->hw[i]) U(DestroyWindow)(r->hw[i]);
}
SCEN(window_handles_have_distinct_index_and_uniq) {
    load_user32();
    struct r4 r; memset(&r, 0, sizeof r);
    GC(gtc_thread(w_wins, &r));
    if (!r.hw[0]) { t_skip("cannot create window"); return; }
    for (int i = 0; i < 3; i++) { GC(r.hw[i] != NULL); GC(r.isw[i]); GC(r.thr[i] != 0); }
    GNE((ULONG_PTR)r.hw[0] & 0xFFFF, (ULONG_PTR)r.hw[1] & 0xFFFF);
    GNE((ULONG_PTR)r.hw[1] & 0xFFFF, (ULONG_PTR)r.hw[2] & 0xFFFF);
    GNE((ULONG_PTR)r.hw[0] & 0xFFFF, (ULONG_PTR)r.hw[2] & 0xFFFF);
    GEQ(r.thr[0], r.thr[1]);
}

/* ---------------------------------------------------------------------------------------- */
/* Thread exit destroys the thread's windows and hooks                                       */
/* ---------------------------------------------------------------------------------------- */

struct ex {
    HANDLE up, go; HWND hw[4]; HHOOK hook; HBITMAP bmp; HMENU menu;
    ULONG c0, c1; DWORD tid;
};
static ULONG (WINAPI *p_ggr)(HANDLE, DWORD);
static LRESULT CALLBACK ex_hook(int c, WPARAM w, LPARAM l) { return U(CallNextHookEx)(NULL, c, w, l); }
static DWORD WINAPI ex_thread(LPVOID p) {
    struct ex *e = (struct ex *)p;
    e->tid = GetCurrentThreadId();
    e->c0 = p_ggr(GetCurrentProcess(), 1);
    for (int i = 0; i < 4; i++) e->hw[i] = k_make_window("gtc_exit");
    HHOOK (WINAPI *shk)(int, HOOKPROC, HINSTANCE, DWORD) = (HHOOK (WINAPI *)(int, HOOKPROC, HINSTANCE, DWORD))GetProcAddress(GetModuleHandleA("user32.dll"), "SetWindowsHookExW");
    e->hook = shk(WH_GETMESSAGE, ex_hook, NULL, GetCurrentThreadId());
    HBITMAP (WINAPI *cb)(int, int, UINT, UINT, const void *) = (HBITMAP (WINAPI *)(int, int, UINT, UINT, const void *))GetProcAddress(GetModuleHandleA("gdi32.dll"), "CreateBitmap");
    e->bmp = cb(2, 2, 1, 1, NULL);
    HMENU (WINAPI *cm)(void) = (HMENU (WINAPI *)(void))GetProcAddress(GetModuleHandleA("user32.dll"), "CreateMenu");
    e->menu = cm();
    e->c1 = p_ggr(GetCurrentProcess(), 1);
    SetEvent(e->up);
    WaitForSingleObject(e->go, 8000);
    return 0;
}

static void exit_setup(struct ex *e, HANDLE *th) {
    memset(e, 0, sizeof *e);
    load_user32();
    p_ggr = (ULONG (WINAPI *)(HANDLE, DWORD))GetProcAddress(GetModuleHandleA("user32.dll"), "GetGuiResources");
    e->up = CreateEventA(NULL, TRUE, FALSE, NULL);
    e->go = CreateEventA(NULL, TRUE, FALSE, NULL);
    *th = CreateThread(NULL, 0, ex_thread, e, 0, NULL);
    WaitForSingleObject(e->up, 8000);
}
static void exit_finish(struct ex *e, HANDLE th) {
    SetEvent(e->go);
    WaitForSingleObject(th, 8000);
    CloseHandle(th); CloseHandle(e->up); CloseHandle(e->go);
    Sleep(50);       /* thread-exit callout runs on the exiting thread before it is signalled, but be lenient */
}

SCEN(thread_exit_destroys_windows) {
    struct ex e; HANDLE th;
    exit_setup(&e, &th);
    if (!e.hw[0]) { t_skip("cannot create windows"); exit_finish(&e, th); return; }
    BOOL (WINAPI *isw)(HWND) = (BOOL (WINAPI *)(HWND))GetProcAddress(GetModuleHandleA("user32.dll"), "IsWindow");
    for (int i = 0; i < 4; i++) GC(isw(e.hw[i]));          /* alive while the creator lives (cross-thread validation) */
    exit_finish(&e, th);
    for (int i = 0; i < 4; i++) { GCF(!isw(e.hw[i]), "window %d survived its thread", i); }
}

SCEN(thread_exit_window_ops_fail_afterwards) {
    struct ex e; HANDLE th;
    exit_setup(&e, &th);
    if (!e.hw[0]) { t_skip("cannot create windows"); exit_finish(&e, th); return; }
    DWORD (WINAPI *gtp)(HWND, LPDWORD) = (DWORD (WINAPI *)(HWND, LPDWORD))GetProcAddress(GetModuleHandleA("user32.dll"), "GetWindowThreadProcessId");
    GEQ(gtp(e.hw[0], NULL), e.tid);
    exit_finish(&e, th);
    SetLastError(0);
    GEQ(gtp(e.hw[0], NULL), 0);
    GERR(ERROR_INVALID_WINDOW_HANDLE);
}

SCEN(thread_exit_removes_thread_hooks) {
    struct ex e; HANDLE th;
    exit_setup(&e, &th);
    if (!e.hook) { t_skip("SetWindowsHookEx failed"); exit_finish(&e, th); return; }
    exit_finish(&e, th);
    BOOL (WINAPI *unh)(HHOOK) = (BOOL (WINAPI *)(HHOOK))GetProcAddress(GetModuleHandleA("user32.dll"), "UnhookWindowsHookEx");
    SetLastError(0);
    /* the hook object died with its owner thread; NtUserUnhookWindowsHookEx rejects the stale handle */
    GEQ(unh(e.hook), 0);
    GERR(ERROR_INVALID_HOOK_HANDLE);
}

SCEN(thread_exit_returns_user_objects) {
    struct ex e; HANDLE th;
    exit_setup(&e, &th);
    if (!e.hw[0]) { t_skip("cannot create windows"); exit_finish(&e, th); return; }
    ULONG mid = p_ggr(GetCurrentProcess(), 1);
    GCF(mid >= e.c0 + 4, "USER objects with live thread %lu vs %lu at start (4 windows)", mid, e.c0);
    exit_finish(&e, th);
    ULONG after = p_ggr(GetCurrentProcess(), 1);
    /* windows, hook (and the thread's menu is process-owned: allowed to remain) are reclaimed */
    GCF(after + 4 <= mid, "USER objects after exit %lu, before exit %lu", after, mid);
    t_note("USER objects: start %lu, live %lu, after exit %lu (menu %p)", e.c0, mid, after, (void *)e.menu);
}

SCEN(thread_exit_keeps_gdi_objects) {
    struct ex e; HANDLE th;
    exit_setup(&e, &th);
    if (!e.bmp) { t_skip("CreateBitmap failed"); exit_finish(&e, th); return; }
    DWORD (WINAPI *got)(HGDIOBJ) = (DWORD (WINAPI *)(HGDIOBJ))GetProcAddress(GetModuleHandleA("gdi32.dll"), "GetObjectType");
    GEQ(got(e.bmp), OBJ_BITMAP);
    exit_finish(&e, th);
    /* GDI objects are owned by the process, not by the creating thread */
    GEQ(got(e.bmp), OBJ_BITMAP);
    struct k_gdicell c;
    if (k_gdi_cell(e.bmp, &c)) GEQ(c.type_check, (unsigned char)((ULONG_PTR)e.bmp >> 24));
    BOOL (WINAPI *dob)(HGDIOBJ) = (BOOL (WINAPI *)(HGDIOBJ))GetProcAddress(GetModuleHandleA("gdi32.dll"), "DeleteObject");
    GC(dob(e.bmp));
    /* GetObjectType keeps decoding the type from the handle bits of a deleted bitmap on Windows 10, so the
     * shared-table cell is the authority: object type 0 or different upper handle bits means the slot is free. */
    void *tab = NULL; unsigned char cell[GDI_CELL_SIZE];
    if (k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &tab) && tab &&
        k_try_read((char *)tab + (ULONG_PTR)((ULONG_PTR)e.bmp & 0xffff) * GDI_CELL_SIZE, cell, sizeof cell)) {
        unsigned short upper = *(unsigned short *)(cell + 0xc); unsigned char objt = cell[0xe];
        GCF(objt == 0 || upper != (unsigned short)(((ULONG_PTR)e.bmp >> 16) & 0xffff), "cell of the deleted bitmap still live (objt 0x%x)", objt);
    } else t_note("gdi table unreadable; deleted-bitmap cell not checked");
}

SCEN(thread_exit_main_thread_state_unchanged) {
    struct ex e; HANDLE th;
    load_user32();
    BOOL (WINAPI *igt)(BOOL) = (BOOL (WINAPI *)(BOOL))GetProcAddress(GetModuleHandleA("user32.dll"), "IsGUIThread");
    igt(TRUE);
    unsigned long long ti = gtc_teb_u64(TEB_Win32ThreadInfo), fl = gtc_ci_u64(CI_flags_off), hkl = gtc_ci_u64(CI_hKL);
    unsigned long long tbl = gtc_peb_u64(PEB_KernelCallbackTable);
    exit_setup(&e, &th);
    exit_finish(&e, th);
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), ti);
    GEQ(gtc_ci_u64(CI_flags_off), fl);
    GEQ(gtc_ci_u64(CI_hKL), hkl);
    GEQ(gtc_peb_u64(PEB_KernelCallbackTable), tbl);
    GEQ(igt(FALSE), 1);
}

/* ---------------------------------------------------------------------------------------- */
/* Two workers: independent queues, independent conversion                                   */
/* ---------------------------------------------------------------------------------------- */

struct two { HANDLE a_ready, b_done; unsigned long long a_ti, b_ti, b_before; DWORD a_tid, b_tid; HWND a_win; DWORD b_sees_a_owner; int b_isw; };
static struct two g_two;
static DWORD WINAPI two_a(LPVOID p) {
    g_two.a_tid = GetCurrentThreadId();
    g_two.a_win = k_make_window("gtc_two_a");
    g_two.a_ti = gtc_teb_u64(TEB_Win32ThreadInfo);
    SetEvent(g_two.a_ready);
    WaitForSingleObject(g_two.b_done, 8000);
    if (g_two.a_win) U(DestroyWindow)(g_two.a_win);
    return 0;
}
static DWORD WINAPI two_b(LPVOID p) {
    g_two.b_tid = GetCurrentThreadId();
    g_two.b_before = gtc_teb_u64(TEB_Win32ThreadInfo);
    WaitForSingleObject(g_two.a_ready, 8000);
    g_two.b_before = gtc_teb_u64(TEB_Win32ThreadInfo);       /* still unconverted although A owns a window */
    g_two.b_isw = U(IsWindow)(g_two.a_win);
    g_two.b_sees_a_owner = U(GetWindowThreadProcessId)(g_two.a_win, NULL);
    g_two.b_ti = gtc_teb_u64(TEB_Win32ThreadInfo);
    SetEvent(g_two.b_done);
    return 0;
}
SCEN(two_workers_conversion_is_per_thread) {
    load_user32();
    memset(&g_two, 0, sizeof g_two);
    g_two.a_ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_two.b_done = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE a = CreateThread(NULL, 0, two_a, NULL, 0, NULL), b = CreateThread(NULL, 0, two_b, NULL, 0, NULL);
    WaitForSingleObject(b, 8000); WaitForSingleObject(a, 8000);
    CloseHandle(a); CloseHandle(b); CloseHandle(g_two.a_ready); CloseHandle(g_two.b_done);
    if (!g_two.a_win) { t_skip("cannot create window"); return; }
    GEQ(g_two.a_ti, g_two.a_tid);
    GEQ(g_two.b_before, 0);
    GC(g_two.b_isw);                                     /* IsWindow works from any thread... */
    GEQ(g_two.b_sees_a_owner, g_two.a_tid);
    /* ...but NtUserQueryWindow on B converted B as a side effect of the syscall */
    GEQ(g_two.b_ti, g_two.b_tid);
}

/* ---------------------------------------------------------------------------------------- */
/* GetGuiResources through the user32 path                                                  */
/* ---------------------------------------------------------------------------------------- */

SCEN(guiresources_bitmaps_exact_delta) {
    load_user32();
    ULONG (WINAPI *ggr)(HANDLE, DWORD) = (ULONG (WINAPI *)(HANDLE, DWORD))GetProcAddress(GetModuleHandleA("user32.dll"), "GetGuiResources");
    HBITMAP (WINAPI *cb)(int, int, UINT, UINT, const void *) = (HBITMAP (WINAPI *)(int, int, UINT, UINT, const void *))GetProcAddress(GetModuleHandleA("gdi32.dll"), "CreateBitmap");
    BOOL (WINAPI *dob)(HGDIOBJ) = (BOOL (WINAPI *)(HGDIOBJ))GetProcAddress(GetModuleHandleA("gdi32.dll"), "DeleteObject");
    HBITMAP warm = cb(1, 1, 1, 1, NULL); dob(warm);
    ULONG base = ggr(GetCurrentProcess(), 0), peak0 = ggr(GetCurrentProcess(), 2);
    HBITMAP b[25];
    for (int i = 0; i < 25; i++) b[i] = cb(1, 1, 1, 1, NULL);
    ULONG mid = ggr(GetCurrentProcess(), 0), peak1 = ggr(GetCurrentProcess(), 2);
    GCF(mid == base + 25, "GDI count %lu -> %lu after 25 bitmaps", base, mid);
    GC(peak1 >= mid);
    GC(peak1 >= peak0);
    for (int i = 0; i < 25; i++) dob(b[i]);
    ULONG end = ggr(GetCurrentProcess(), 0), peak2 = ggr(GetCurrentProcess(), 2);
    GCF(end == base, "GDI count %lu after delete, base %lu", end, base);
    GCF(peak2 >= peak1, "GDI peak went down: %lu -> %lu", peak1, peak2);  /* peaks never decrease */
}

SCEN(guiresources_menus_exact_delta) {
    load_user32();
    ULONG (WINAPI *ggr)(HANDLE, DWORD) = (ULONG (WINAPI *)(HANDLE, DWORD))GetProcAddress(GetModuleHandleA("user32.dll"), "GetGuiResources");
    HMENU (WINAPI *cm)(void) = (HMENU (WINAPI *)(void))GetProcAddress(GetModuleHandleA("user32.dll"), "CreateMenu");
    BOOL (WINAPI *dm)(HMENU) = (BOOL (WINAPI *)(HMENU))GetProcAddress(GetModuleHandleA("user32.dll"), "DestroyMenu");
    HMENU w = cm(); dm(w);
    ULONG base = ggr(GetCurrentProcess(), 1), peak0 = ggr(GetCurrentProcess(), 4);
    HMENU m[10];
    for (int i = 0; i < 10; i++) m[i] = cm();
    ULONG mid = ggr(GetCurrentProcess(), 1), peak1 = ggr(GetCurrentProcess(), 4);
    GCF(mid == base + 10, "USER count %lu -> %lu after 10 menus", base, mid);
    GC(peak1 >= mid);
    GC(peak1 >= peak0);
    for (int i = 0; i < 10; i++) dm(m[i]);
    GCF(ggr(GetCurrentProcess(), 1) == base, "USER count after destroy");
    GC(ggr(GetCurrentProcess(), 4) >= peak1);
}

SCEN(guiresources_window_counts) {
    load_user32();
    ULONG (WINAPI *ggr)(HANDLE, DWORD) = (ULONG (WINAPI *)(HANDLE, DWORD))GetProcAddress(GetModuleHandleA("user32.dll"), "GetGuiResources");
    HWND w0 = k_make_window("gtc_warm");
    if (!w0) { t_skip("cannot create window"); return; }
    U(DestroyWindow)(w0);
    ULONG base = ggr(GetCurrentProcess(), 1);
    HWND w[6];
    for (int i = 0; i < 6; i++) w[i] = k_make_window("gtc_cnt");
    ULONG mid = ggr(GetCurrentProcess(), 1);
    GCF(mid >= base + 6, "USER count %lu -> %lu after 6 windows", base, mid);
    t_note("USER objects per window: %.2f", (mid - base) / 6.0);
    for (int i = 0; i < 6; i++) U(DestroyWindow)(w[i]);
    GCF(ggr(GetCurrentProcess(), 1) == base, "USER count after destroy %lu base %lu", ggr(GetCurrentProcess(), 1), base);
}

struct gctx { ULONG (WINAPI *g)(HANDLE, DWORD); ULONG *out; };
static void gres_thr(void *p) { struct gctx *c = (struct gctx *)p; *c->out = c->g(GetCurrentProcess(), 0); }
SCEN(guiresources_main_thread_workers_share_process_counters) {
    load_user32();
    ULONG (WINAPI *ggr)(HANDLE, DWORD) = (ULONG (WINAPI *)(HANDLE, DWORD))GetProcAddress(GetModuleHandleA("user32.dll"), "GetGuiResources");
    HBITMAP (WINAPI *cb)(int, int, UINT, UINT, const void *) = (HBITMAP (WINAPI *)(int, int, UINT, UINT, const void *))GetProcAddress(GetModuleHandleA("gdi32.dll"), "CreateBitmap");
    BOOL (WINAPI *dob)(HGDIOBJ) = (BOOL (WINAPI *)(HGDIOBJ))GetProcAddress(GetModuleHandleA("gdi32.dll"), "DeleteObject");
    HBITMAP warm = cb(1, 1, 1, 1, NULL); dob(warm);
    ULONG base = ggr(GetCurrentProcess(), 0);
    HBITMAP b = cb(1, 1, 1, 1, NULL);      /* made on the main thread */
    ULONG seen = 0;
    struct gctx ctx = { ggr, &seen };
    GC(gtc_thread(gres_thr, &ctx));
    GEQ(seen, base + 1);                    /* the counter is per process, not per thread */
    dob(b);
}

/* ---------------------------------------------------------------------------------------- */
/* GDI batching state in the TEB                                                            */
/* ---------------------------------------------------------------------------------------- */

SCEN(gdi_batch_limit_default_and_range) {
    load_user32();
    HMODULE g = GetModuleHandleA("gdi32.dll");
    DWORD (WINAPI *get)(void) = (DWORD (WINAPI *)(void))GetProcAddress(g, "GdiGetBatchLimit");
    DWORD (WINAPI *set)(DWORD) = (DWORD (WINAPI *)(DWORD))GetProcAddress(g, "GdiSetBatchLimit");
    if (!get || !set) { t_skip("no GdiGet/SetBatchLimit"); return; }
    GEQ(get(), 20);                          /* default batch limit */
    GEQ(set(5), 20);                         /* returns the previous limit */
    GEQ(get(), 5);
    GEQ(set(0), 5);                          /* 0 means "default" == 0x14 */
    GEQ(get(), 20);
    GEQ(set(21), 0);                         /* > 0x14 is rejected: returns 0, limit unchanged */
    GEQ(get(), 20);
    GEQ(set(0xFFFFFFFF), 0);
    GEQ(get(), 20);
    GEQ(set(1), 20);
    GEQ(get(), 1);
    GEQ(set(20), 1);
    GEQ(get(), 20);
}

SCEN(gdi_batch_flush_empties_teb_batch) {
    load_user32();
    HMODULE g = GetModuleHandleA("gdi32.dll");
    HDC (WINAPI *ccd)(HDC) = (HDC (WINAPI *)(HDC))GetProcAddress(g, "CreateCompatibleDC");
    HBITMAP (WINAPI *cb)(int, int, UINT, UINT, const void *) = (HBITMAP (WINAPI *)(int, int, UINT, UINT, const void *))GetProcAddress(g, "CreateBitmap");
    HGDIOBJ (WINAPI *so)(HDC, HGDIOBJ) = (HGDIOBJ (WINAPI *)(HDC, HGDIOBJ))GetProcAddress(g, "SelectObject");
    BOOL (WINAPI *pat)(HDC, int, int, int, int, DWORD) = (BOOL (WINAPI *)(HDC, int, int, int, int, DWORD))GetProcAddress(g, "PatBlt");
    BOOL (WINAPI *flush)(void) = (BOOL (WINAPI *)(void))GetProcAddress(g, "GdiFlush");
    BOOL (WINAPI *ddc)(HDC) = (BOOL (WINAPI *)(HDC))GetProcAddress(g, "DeleteDC");
    BOOL (WINAPI *dob)(HGDIOBJ) = (BOOL (WINAPI *)(HGDIOBJ))GetProcAddress(g, "DeleteObject");
    HDC dc = ccd(NULL); HBITMAP bm = cb(16, 16, 1, 1, NULL);
    if (!dc || !bm) { t_skip("DC/bitmap creation failed"); return; }
    HGDIOBJ old = so(dc, bm);
    for (int i = 0; i < 6; i++) pat(dc, 0, 0, 4, 4, BLACKNESS);
    unsigned long queued = (unsigned long)(gtc_teb_u64(TEB_GdiBatchCount) & 0xFFFFFFFF);
    t_note("GdiBatchCount after 6 PatBlt: %lu (batching is gdi32-internal; value is informational)", queued);
    GC(queued <= 0x14);
    GC(flush());
    /* NtGdiFlush consumes the batch: nothing remains queued afterwards */
    GEQ(gtc_teb_u64(TEB_GdiBatchCount) & 0xFFFFFFFF, 0);
    GEQ(gtc_teb_u64(TEB_GdiTebBatch) & 0x7FFFFFFF, 0);
    so(dc, old); ddc(dc); dob(bm);
}

SCEN(gdi_batch_limit_one_disables_batching) {
    load_user32();
    HMODULE g = GetModuleHandleA("gdi32.dll");
    DWORD (WINAPI *set)(DWORD) = (DWORD (WINAPI *)(DWORD))GetProcAddress(g, "GdiSetBatchLimit");
    HDC (WINAPI *ccd)(HDC) = (HDC (WINAPI *)(HDC))GetProcAddress(g, "CreateCompatibleDC");
    HBITMAP (WINAPI *cb)(int, int, UINT, UINT, const void *) = (HBITMAP (WINAPI *)(int, int, UINT, UINT, const void *))GetProcAddress(g, "CreateBitmap");
    HGDIOBJ (WINAPI *so)(HDC, HGDIOBJ) = (HGDIOBJ (WINAPI *)(HDC, HGDIOBJ))GetProcAddress(g, "SelectObject");
    BOOL (WINAPI *pat)(HDC, int, int, int, int, DWORD) = (BOOL (WINAPI *)(HDC, int, int, int, int, DWORD))GetProcAddress(g, "PatBlt");
    BOOL (WINAPI *ddc)(HDC) = (BOOL (WINAPI *)(HDC))GetProcAddress(g, "DeleteDC");
    BOOL (WINAPI *dob)(HGDIOBJ) = (BOOL (WINAPI *)(HGDIOBJ))GetProcAddress(g, "DeleteObject");
    if (!set) { t_skip("no GdiSetBatchLimit"); return; }
    HDC dc = ccd(NULL); HBITMAP bm = cb(16, 16, 1, 1, NULL);
    if (!dc || !bm) { t_skip("DC/bitmap creation failed"); return; }
    HGDIOBJ old = so(dc, bm);
    DWORD prev = set(1);
    for (int i = 0; i < 8; i++) {
        pat(dc, 0, 0, 4, 4, WHITENESS);
        GCF((gtc_teb_u64(TEB_GdiBatchCount) & 0xFFFFFFFF) == 0, "iteration %d: batch count nonzero with limit 1", i);
    }
    set(prev ? prev : 20);
    so(dc, old); ddc(dc); dob(bm);
}

/* ---------------------------------------------------------------------------------------- */
/* gdi32 alone                                                                              */
/* ---------------------------------------------------------------------------------------- */

SCEN(gdi32_load_converts_loading_thread) {
    gtc_assert_pristine();
    HMODULE g = t_module("gdi32.dll");
    if (!g) { t_skip("no gdi32"); return; }
    /* gdi32full!GdiDllInitialize(PROCESS_ATTACH) -> NtGdiInit on the loader thread converts it, and
     * GdiProcessSetup's import of user32!InitializeLpkHooks drags user32 (and its DllMain) in. */
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), GetCurrentThreadId());
    GNE(gtc_peb_u64(PEB_GdiSharedHandleTable), 0);
    HDC (WINAPI *ccd)(HDC) = (HDC (WINAPI *)(HDC))GetProcAddress(g, "CreateCompatibleDC");
    BOOL (WINAPI *ddc)(HDC) = (BOOL (WINAPI *)(HDC))GetProcAddress(g, "DeleteDC");
    HDC dc = ccd(NULL);
    GC(dc != NULL);
    struct k_gdicell c;
    if (dc && k_gdi_cell(dc, &c)) GEQ(c.type_check, (unsigned char)((ULONG_PTR)dc >> 24));
    if (GetModuleHandleA("user32.dll") == NULL) GEQ(gtc_peb_u64(PEB_KernelCallbackTable), 0);
    else GNE(gtc_peb_u64(PEB_KernelCallbackTable), 0);
    if (dc) ddc(dc);
}

/* ---------------------------------------------------------------------------------------- */
/* GUI-subsystem image: identical rules through user32                                      */
/* ---------------------------------------------------------------------------------------- */

TEST(gui_thread_conversion_gui_subsystem_user32_load) { gtc_parent_run_gui("user32_load_sets_callback_table", 30000); }
TEST(gui_thread_conversion_gui_subsystem_user32_slot) { gtc_parent_run_gui("user32_load_callback_slot_clientthreadsetup", 30000); }
TEST(gui_thread_conversion_gui_subsystem_isguithread) { gtc_parent_run_gui("isguithread_true_converts_then_false_reports_true", 30000); }
TEST(gui_thread_conversion_gui_subsystem_trigger_getkeystate) { gtc_parent_run_gui("trigger:1", 30000); }
TEST(gui_thread_conversion_gui_subsystem_thread_exit) { gtc_parent_run_gui("thread_exit_destroys_windows", 30000); }
TEST(gui_thread_conversion_gui_subsystem_guires) { gtc_parent_run_gui("guires_flags_self_valid", 30000); }
