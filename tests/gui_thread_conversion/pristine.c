/* gui_thread_conversion / pristine: process and thread state before and after the FIRST win32k
 * syscall, driven only through win32u.dll stubs (user32/gdi32 never load in these children).
 *
 * Kernel facts these checks rest on (Windows 10 19041 analysis):
 *  - A thread is converted to a GUI thread lazily: the first service number >= 0x1000 faults into
 *    nt!KiBBTUnexpectedRange -> PsConvertToGuiThread -> win32k process callout (GdiProcessCallout /
 *    the user process callout) + thread callout (UserThreadCallout).
 *  - GdiProcessCallout (create): PEB.GdiDCAttributeList = 0x14, PEB.GdiHandleBuffer (0xF0 bytes at
 *    PEB+0x140) zeroed, PEB.GdiSharedHandleTable = base of a PAGE_READONLY view of the shared
 *    handle-table section.
 *  - thread-info creation stores the thread id (PsGetThreadId(pti->pEThread)) at TEB+0x78, i.e. on
 *    19041 TEB.Win32ThreadInfo is NOT a kernel pointer; it is merely a non-zero "converted" marker
 *    that equals the thread id. It also points pti->pClientInfo at TEB+0x800.
 *  - the kernel thread-setup code fills CLIENTINFO: +0x10 dwExpWinVer, +0x1c TIF flags, +0xd0 = 0,
 *    +0x90 hKL / +0x98 CodePage from the active keyboard layout.
 *  - PEB.KernelCallbackTable (PEB+0x58) is written only by user32's DllMain.
 *  - The conversion calls back into user mode through that table (ClientThreadSetup). With no user32 the table is NULL and the
 *    first win32k syscall kills the process (see first_syscall_without_callback_table_is_fatal), so the scenarios that need a
 *    converted but user32-less thread install a table of do-nothing handlers first (W(), gtc_win32u_proc). */
#include "gtc.h"

/* ======================================================================================== */
/* Start-of-process state                                                                   */
/* ======================================================================================== */

SCEN(start_win32threadinfo_null) {
    gtc_assert_pristine();
    /* IsGUIThread(FALSE) tests exactly this word; nothing converted a fresh thread yet. */
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), 0);
}

SCEN(start_clientinfo_all_zero) {
    gtc_assert_pristine();
    /* TEB.Win32ClientInfo is zero-initialised at thread creation; the kernel thread setup is the first writer. */
    for (int i = 0; i < CI_SIZE_QWORDS; i++) GCF(gtc_ci_u64(i * 8) == 0, "CLIENTINFO qword %d = 0x%llx", i, gtc_ci_u64(i * 8));
}

SCEN(start_kernel_callback_table_null) {
    gtc_assert_pristine();
    /* Only user32!_UserClientDllInitialize writes PEB+0x58 (apfnDispatch). */
    GEQ(gtc_peb_u64(PEB_KernelCallbackTable), 0);
    GEQ(gtc_peb_u64(PEB_UserSharedInfoPtr), 0);
}

SCEN(start_gdi_shared_handle_table_null) {
    gtc_assert_pristine();
    /* Written by GdiProcessCallout when the process first becomes a win32k process. */
    GEQ(gtc_peb_u64(PEB_GdiSharedHandleTable), 0);
}

SCEN(start_gdi_dc_attribute_list_zero) {
    gtc_assert_pristine();
    GEQ(gtc_peb_u32(PEB_GdiDCAttributeList_off), 0);
}

SCEN(start_gdi_handle_buffer_zero) {
    gtc_assert_pristine();
    for (int i = 0; i < 60; i++) GCF(gtc_peb_u32(PEB_GdiHandleBuffer_off + i * 4) == 0, "GdiHandleBuffer[%d]", i);
}

SCEN(start_teb_gdi_batch_empty) {
    gtc_assert_pristine();
    GEQ(gtc_teb_u64(TEB_GdiTebBatch + GDI_TEB_BATCH_Offset_HasRenderingCommand) & 0xFFFFFFFF, 0);
    GEQ(gtc_teb_u64(TEB_GdiTebBatch + GDI_TEB_BATCH_HDC), 0);
    GEQ(gtc_teb_u64(TEB_GdiBatchCount) & 0xFFFFFFFF, 0);
}

SCEN(start_win32u_stub_shape) {
    HMODULE m = t_module("win32u.dll");
    GC(m != NULL);
    /* Loading win32u alone must not connect or convert anything. */
    gtc_assert_pristine();
    GEQ(gtc_peb_u64(PEB_KernelCallbackTable), 0);
    GEQ(gtc_peb_u64(PEB_GdiSharedHandleTable), 0);
    FARPROC p = GetProcAddress(m, "NtUserGetThreadState");
    GC(p != NULL);
    if (p) { GC(k_stub_valid(p)); if (!t_is_wine()) GEQ(k_stub_number(p), 4096); }
    /* resolving an export is not a syscall */
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), 0);
}

/* ======================================================================================== */
/* The first syscall converts the thread (any win32k service number does)                   */
/* ======================================================================================== */

typedef void (*first_fn)(void);
static void f_getthreadstate(void) { NtUserGetThreadState_pfn f = W(NtUserGetThreadState); if (f) f(0xe); }
static void f_getkeystate(void)    { NtUserGetKeyState_pfn f = W(NtUserGetKeyState); if (f) f(VK_SHIFT); }
static void f_getkbdlayout(void)   { NtUserGetKeyboardLayout_pfn f = W(NtUserGetKeyboardLayout); if (f) f(0); }
static void f_getthreaddesk(void)  { NtUserGetThreadDesktop_pfn f = W(NtUserGetThreadDesktop); if (f) f(GetCurrentThreadId()); }
static void f_getforeground(void)  { NtUserGetForegroundWindow_pfn f = W(NtUserGetForegroundWindow); if (f) f(); }
static void f_querywindow(void)    { NtUserQueryWindow_pfn f = W(NtUserQueryWindow); if (f) f(NULL, 5); }
static void f_gdiflush(void)       { NtGdiFlush_pfn f = W(NtGdiFlush); if (f) f(); }
static void f_queuestatus(void)    { NtUserGetQueueStatus_pfn f = W(NtUserGetQueueStatus); if (f) f(0x4FF); }
static void f_dblclick(void)       { NtUserGetDoubleClickTime_pfn f = W(NtUserGetDoubleClickTime); if (f) f(); }
static void f_getwinsta(void)      { NtUserGetProcessWindowStation_pfn f = W(NtUserGetProcessWindowStation); if (f) f(); }
static void f_guiresources(void)   { NtUserGetGuiResources_pfn f = W(NtUserGetGuiResources); if (f) f((HANDLE)(LONG_PTR)-1, 0); }
static void f_processconnect(void) { NtUserProcessConnect_pfn f = W(NtUserProcessConnect); unsigned char buf[0x240]; memset(buf, 0, sizeof buf); if (f) f((HANDLE)(LONG_PTR)-1, buf); }

static const struct { const char *name; first_fn fn; } g_first[] = {
    { "NtUserGetThreadState", f_getthreadstate }, { "NtUserGetKeyState", f_getkeystate },
    { "NtUserGetKeyboardLayout", f_getkbdlayout }, { "NtUserGetThreadDesktop", f_getthreaddesk },
    { "NtUserGetForegroundWindow", f_getforeground }, { "NtUserQueryWindow", f_querywindow },
    { "NtGdiFlush", f_gdiflush }, { "NtUserGetQueueStatus", f_queuestatus },
    { "NtUserGetDoubleClickTime", f_dblclick }, { "NtUserGetProcessWindowStation", f_getwinsta },
    { "NtUserGetGuiResources", f_guiresources }, { "NtUserProcessConnect", f_processconnect },
};

/* Body shared by the first_* TESTs: arg = index into g_first. */
SCEN_BODY(first_syscall) {
    int idx = atoi(gtc_arg());
    gtc_assert_pristine();
    GCF(idx >= 0 && idx < (int)(sizeof g_first / sizeof g_first[0]), "bad index %d", idx);
    if (idx < 0 || idx >= (int)(sizeof g_first / sizeof g_first[0])) return;
    t_module("win32u.dll");
    g_first[idx].fn();
    /* PsConvertToGuiThread ran before the service routine: whichever win32k syscall came first,
     * TEB.Win32ThreadInfo is now set (to the thread id) and the process owns a GDI handle table. */
    GCF(gtc_teb_u64(TEB_Win32ThreadInfo) != 0, "%s did not convert the thread", g_first[idx].name);
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), GetCurrentThreadId());
    GC(gtc_peb_u64(PEB_GdiSharedHandleTable) != 0);
    GEQ(gtc_peb_u32(PEB_GdiDCAttributeList_off), 0x14);
    /* still no user32: the callback table is the do-nothing one the lookup installed */
    GEQ(gtc_peb_u64(PEB_KernelCallbackTable), gtc_stub_table());
    GC(GetModuleHandleA("user32.dll") == NULL);
}
SCEN_ARG(first_syscall_getthreadstate, first_syscall, "0")
SCEN_ARG(first_syscall_getkeystate, first_syscall, "1")
SCEN_ARG(first_syscall_getkeyboardlayout, first_syscall, "2")
SCEN_ARG(first_syscall_getthreaddesktop, first_syscall, "3")
SCEN_ARG(first_syscall_getforegroundwindow, first_syscall, "4")
SCEN_ARG(first_syscall_querywindow, first_syscall, "5")
SCEN_ARG(first_syscall_gdiflush, first_syscall, "6")
SCEN_ARG(first_syscall_getqueuestatus, first_syscall, "7")
SCEN_ARG(first_syscall_getdoubleclicktime, first_syscall, "8")
SCEN_ARG(first_syscall_getprocesswindowstation, first_syscall, "9")
SCEN_ARG(first_syscall_getguiresources, first_syscall, "10")
SCEN_ARG(first_syscall_processconnect, first_syscall, "11")

/* What really happens to a process that has no user32 (NULL callback table) and makes its first win32k syscall on a worker
 * thread: the kernel converts the thread, the user-mode callback cannot be dispatched, and the process is terminated with
 * STATUS_FATAL_USER_CALLBACK_EXCEPTION. */
static DWORD WINAPI fatal_thread(LPVOID p) {
    typedef ULONG_PTR (NTAPI *fn_t)(ULONG);
    fn_t f = (fn_t)t_proc_in("win32u.dll", "NtUserGetThreadState");
    if (f) f(0xe);
    return 0;
}
SCEN_BODY(fatal_worker) {
    gtc_assert_pristine();
    GEQ(gtc_peb_u64(PEB_KernelCallbackTable), 0);
    HANDLE th = CreateThread(NULL, 0, fatal_thread, NULL, 0, NULL);
    GC(th != NULL);
    if (th) WaitForSingleObject(th, 10000);
    printf("  NOTE the process survived its first win32k syscall without user32\n");
}
TEST(gui_thread_conversion_first_syscall_without_callback_table_is_fatal) {
    if (t_is_wine()) { t_skip("Wine has no GUI-thread conversion"); return; }
    struct k_child_result r = k_child_run("gtc", "fatal_worker", 30000);
    CHECKF(r.exit_code == 0xC000041D, "child exit 0x%lx, want 0xc000041d (STATUS_FATAL_USER_CALLBACK_EXCEPTION)", (unsigned long)r.exit_code);
}

SCEN(first_call_getthreadstate_reports_gui) {
    gtc_assert_pristine();
    NtUserGetThreadState_pfn f = W(NtUserGetThreadState);
    if (!f) { t_skip("no NtUserGetThreadState"); return; }
    /* win32kfull!NtUserGetThreadState: selector 0xE returns 1 unconditionally once EnterSharedCrit
     * (which performs the conversion) succeeded. This is what IsGUIThread(TRUE) relies on. */
    GEQ(f(0xe), 1);
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), GetCurrentThreadId());
}

SCEN(first_call_thread_info_is_thread_id) {
    gtc_assert_pristine();
    NtUserGetThreadState_pfn f = W(NtUserGetThreadState);
    if (!f) { t_skip("no NtUserGetThreadState"); return; }
    f(0xe);
    unsigned long long v = gtc_teb_u64(TEB_Win32ThreadInfo);
    /* thread-info creation: TEB+0x78 = PsGetThreadId(...). Hence not a kernel address, and equal to
     * both GetCurrentThreadId() and TEB.ClientId.UniqueThread. */
    GC(!gtc_is_kernel_range(v));
    GEQ(v, GetCurrentThreadId());
    GEQ(gtc_teb_u64(TEB_ClientIdThread), GetCurrentThreadId());
    t_info("Win32ThreadInfo=0x%llx tid=%lu", v, GetCurrentThreadId());
}

SCEN(first_call_is_idempotent) {
    gtc_assert_pristine();
    NtUserGetThreadState_pfn f = W(NtUserGetThreadState);
    if (!f) { t_skip("no NtUserGetThreadState"); return; }
    f(0xe);
    unsigned long long v = gtc_teb_u64(TEB_Win32ThreadInfo);
    unsigned long long tbl = gtc_peb_u64(PEB_GdiSharedHandleTable);
    for (int i = 0; i < 20; i++) GEQ(f(0xe), 1);
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), v);          /* a second call must not re-convert */
    GEQ(gtc_peb_u64(PEB_GdiSharedHandleTable), tbl);
    GEQ(gtc_peb_u32(PEB_GdiDCAttributeList_off), 0x14);
}

SCEN(first_call_gdi_table_is_readonly_mapped_view) {
    gtc_assert_pristine();
    NtGdiFlush_pfn f = W(NtGdiFlush);
    if (!f) { t_skip("no NtGdiFlush"); return; }
    f();
    unsigned long long t = gtc_peb_u64(PEB_GdiSharedHandleTable);
    GC(t != 0);
    if (!t) return;
    /* GdiProcessCallout: ZwMapViewOfSection(gpHmgrSharedHandleSection, ..., Protect=PAGE_READONLY),
     * so the table is a section view the process can read but never write. */
    DWORD type = 0; SIZE_T region = 0;
    DWORD prot = gtc_query((void *)(ULONG_PTR)t, &type, &region);
    GEQ(prot, PAGE_READONLY);
    GEQ(type, MEM_MAPPED);
    GEQ(t & 0xFFF, 0);                     /* a view base is page aligned (64K granularity actually) */
    GEQ(t & 0xFFFF, 0);
    GC(region >= 0x1000);
    GC(t < 0x7FFFFFFF0000ULL);             /* user-mode address */
    unsigned char b; GC(k_try_read((void *)(ULONG_PTR)t, &b, 1));
}

SCEN(first_call_handle_buffer_stays_zero) {
    gtc_assert_pristine();
    NtGdiFlush_pfn f = W(NtGdiFlush);
    if (!f) { t_skip("no NtGdiFlush"); return; }
    f();
    /* GdiProcessCallout memset(PEB+0x140, 0, 0xF0) */
    for (int i = 0; i < 60; i++) GCF(gtc_peb_u32(PEB_GdiHandleBuffer_off + i * 4) == 0, "GdiHandleBuffer[%d]", i);
}

SCEN(first_call_dc_attribute_list_is_20) {
    gtc_assert_pristine();
    NtGdiFlush_pfn f = W(NtGdiFlush);
    if (!f) { t_skip("no NtGdiFlush"); return; }
    f();
    GEQ(gtc_peb_u32(PEB_GdiDCAttributeList_off), 20);
}

SCEN(first_call_gdi_teb_batch_untouched) {
    gtc_assert_pristine();
    NtGdiFlush_pfn f = W(NtGdiFlush);
    if (!f) { t_skip("no NtGdiFlush"); return; }
    f();
    /* the batch is a gdi32 (user-mode) structure; the kernel conversion never queues commands */
    GEQ(gtc_teb_u64(TEB_GdiBatchCount) & 0xFFFFFFFF, 0);
    GEQ(gtc_teb_u64(TEB_GdiTebBatch) & 0xFFFFFFFF, 0);
    GEQ(gtc_teb_u64(TEB_GdiTebBatch + GDI_TEB_BATCH_HDC), 0);
}

/* ======================================================================================== */
/* CLIENTINFO after a win32u-only conversion                                                */
/* ======================================================================================== */

static void convert_now(void) {
    NtUserGetThreadState_pfn f = W(NtUserGetThreadState);
    if (f) f(0xe);
}

SCEN(clientinfo_expwinver_nonzero) {
    gtc_assert_pristine();
    convert_now();
    unsigned long v = gtc_ci_u32(CI_dwExpWinVer);
    /* thread-info creation: pti->dwExpWinVer = RtlGetExpWinVer(image base) or 0x600 fallback;
     * thread setup copies it to CLIENTINFO+0x10. Never 0 for a process with a PEB. */
    GCF(v != 0, "dwExpWinVer=0x%lx", v);
    t_note("dwExpWinVer=0x%lx", v);
}

SCEN(clientinfo_flags_bit3_clear_without_user32) {
    gtc_assert_pristine();
    convert_now();
    /* user32!ClientThreadSetup returns early when CI_flags & 8; it is the code that sets it. With no
     * user32 the callback (KeUserModeCallback 0x54) cannot run. */
    GEQ(gtc_ci_u64(CI_flags_off) & 8, 0);
    t_note("CI_flags=0x%llx", gtc_ci_u64(CI_flags_off));
}

SCEN(clientinfo_registered_classes_ptr_zero) {
    gtc_assert_pristine();
    convert_now();
    /* thread setup: *(CI+0xd0) = 0; only user32!ClientThreadSetup later points it at gdwRegisteredClasses */
    GEQ(gtc_ci_u64(CI_pRegClasses), 0);
}

SCEN(clientinfo_dwtiflags_matches_process_kind) {
    gtc_assert_pristine();
    convert_now();
    unsigned long f = gtc_ci_u32(CI_dwTIFlags);
    t_note("dwTIFlags=0x%lx", f);
 /* the thread-info flags only ever gain 0x100/0x2000000/0x20000000/0x48; an ordinary
     * non-csrss, non-logon process never has the csrss bits 0x2000048 */
    GEQ(f & 0x48, 0);
}

SCEN(clientinfo_keyboard_layout_consistent) {
    gtc_assert_pristine();
    convert_now();
    unsigned long long hkl = gtc_ci_u64(CI_hKL);
    unsigned short cp = gtc_ci_u16(CI_CodePage);
    t_note("CLIENTINFO hKL=0x%llx CodePage=%u", hkl, cp);
    if (!hkl) { t_skip("no active keyboard layout assigned to thread (headless?)"); return; }
    /* thread setup: +0x90 = pkl->hkl, +0x98 = pkl->CodePage */
    GC(cp != 0);
    GC(LOWORD(hkl) != 0);                       /* low word is a language id */
    NtUserGetKeyboardLayout_pfn f = W(NtUserGetKeyboardLayout);
    if (f && f(0) != hkl) t_note("NtUserGetKeyboardLayout(0)=0x%llx differs from CLIENTINFO hKL", (unsigned long long)f(0));
}

SCEN(clientinfo_client_thread_info_fields) {
    gtc_assert_pristine();
    convert_now();
    /* thread setup zeroes the high dwords of the two CLIENTTHREADINFO words at +0xe8/+0xf0 */
    GEQ(gtc_ci_u32(CI_ctiA + 4), 0);
    GEQ(gtc_ci_u32(CI_ctiB + 4), 0);
    /* TEB+0x8e8 is the very same word as CLIENTINFO+0xe8 (thread-info creation writes both names) */
    GEQ(gtc_teb_u64(0x8e8) & 0xFFFFFFFF, gtc_ci_u32(CI_ctiA));
}

SCEN(clientinfo_dwm_bit_clear) {
    gtc_assert_pristine();
    convert_now();
    /* thread setup: CLIENTINFO+0xe0 bit0 only when the process is dwm.exe */
    GEQ(gtc_ci_u64(0xe0) & 1, 0);
}

SCEN(clientinfo_other_fields_still_zero_without_user32) {
    gtc_assert_pristine();
    convert_now();
    /* pDeskInfo/ulClientDelta are published by user32's thread setup / desktop attach, window cache is
     * filled by user32 validation paths: none ran. */
    GEQ(gtc_ci_u64(CI_hWndCache), 0);
    GEQ(gtc_ci_u64(CI_pWndCache), 0);
    t_note("pDeskInfo=0x%llx ulClientDelta=0x%llx", gtc_ci_u64(CI_pDeskInfo), gtc_ci_u64(CI_ulClientDelta));
}

SCEN(clientinfo_survives_further_syscalls) {
    gtc_assert_pristine();
    convert_now();
    unsigned long long a = gtc_ci_u64(CI_hKL), e = gtc_ci_u32(CI_dwExpWinVer), c = gtc_ci_u16(CI_CodePage);
    NtUserGetForegroundWindow_pfn g = W(NtUserGetForegroundWindow);
    NtGdiFlush_pfn fl = W(NtGdiFlush);
    for (int i = 0; i < 10; i++) { if (g) g(); if (fl) fl(); }
    GEQ(gtc_ci_u64(CI_hKL), a);
    GEQ(gtc_ci_u32(CI_dwExpWinVer), e);
    GEQ(gtc_ci_u16(CI_CodePage), c);
}

/* ======================================================================================== */
/* NtUserGetThreadState selector table (converted thread)                                   */
/* ======================================================================================== */

SCEN(thread_state_selectors) {
    NtUserGetThreadState_pfn f = W(NtUserGetThreadState);
    if (!f) { t_skip("no NtUserGetThreadState"); return; }
    /* selector 0xE: GUI thread */
    GEQ(f(0xe), 1);
    /* 0,1,2 are focus/active/capture window of this thread's queue: none exist yet */
    GEQ(f(0), 0); GEQ(f(1), 0); GEQ(f(2), 0);
    /* 0xD: nonzero only for the logon process / logon desktop threads */
    GEQ(f(0xd), 0);
    /* selectors 5, 0xA, 0xF are booleans */
    ULONGLONG b5 = f(5), ba = f(0xa), bf = f(0xf);
    GC(b5 <= 1); GC(ba <= 1); GC(bf <= 1);
    /* anything above 0xF falls through to the final default (0) */
    for (int s = 0x10; s < 0x40; s++) GCF(f(s) == 0, "selector 0x%x", s);
    GEQ(f(0x7FFFFFFF), 0);
    /* negative selectors take the "< 9" path and match nothing */
    GEQ(f(-1), 0);
    GEQ(f((int)0x80000000), 0);
    for (int s = 3; s <= 12; s++) t_info("selector %d -> 0x%llx", s, f(s));
}

SCEN(thread_state_selector_e_after_other_calls) {
    NtUserGetThreadState_pfn f = W(NtUserGetThreadState);
    if (!f) { t_skip("no NtUserGetThreadState"); return; }
    for (int s = 0; s < 0x20; s++) f(s);
    GEQ(f(0xe), 1);
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), GetCurrentThreadId());
}

/* ======================================================================================== */
/* NtUserProcessConnect called from an ordinary process                                      */
/* ======================================================================================== */

SCEN(process_connect_rejected_for_non_csrss) {
    gtc_assert_pristine();
    NtUserProcessConnect_pfn f = W(NtUserProcessConnect);
    if (!f) { t_skip("no NtUserProcessConnect"); return; }
    unsigned char buf[0x240];
    memset(buf, 0xCC, sizeof buf);
    LONG st = f((HANDLE)(LONG_PTR)-1, buf);
    /* win32kfull!NtUserProcessConnect fails with
     * STATUS_UNSUCCESSFUL unless the caller is csrss: only csrss may connect a process; normal processes get their
     * SHAREDINFO through user32's CsrClientConnectToServer. */
    GEQ((ULONG)st, 0xC0000001);
    for (size_t i = 0; i < sizeof buf; i++) if (buf[i] != 0xCC) { GCF(0, "output buffer modified at +0x%zx", i); break; }
    /* the syscall still converted the thread on the way in */
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), GetCurrentThreadId());
    GEQ(gtc_peb_u64(PEB_KernelCallbackTable), gtc_stub_table());
}

SCEN(process_connect_rejected_with_bad_handle) {
    NtUserProcessConnect_pfn f = W(NtUserProcessConnect);
    if (!f) { t_skip("no NtUserProcessConnect"); return; }
    unsigned char buf[0x240]; memset(buf, 0, sizeof buf);
    /* the csrss check precedes handle validation, so every handle value gives the same status */
    HANDLE hs[] = { NULL, (HANDLE)(LONG_PTR)-1, (HANDLE)(LONG_PTR)-2, (HANDLE)0x7FFFFFF0, GetCurrentProcess() };
    for (size_t i = 0; i < sizeof hs / sizeof hs[0]; i++) GEQ((ULONG)f(hs[i], buf), 0xC0000001);
}

SCEN(process_connect_does_not_set_process_state) {
    NtUserProcessConnect_pfn f = W(NtUserProcessConnect);
    if (!f) { t_skip("no NtUserProcessConnect"); return; }
    unsigned char buf[0x240]; memset(buf, 0, sizeof buf);
    f((HANDLE)(LONG_PTR)-1, buf);
    GEQ(gtc_peb_u64(PEB_KernelCallbackTable), gtc_stub_table());
    GEQ(gtc_ci_u64(CI_flags_off) & 8, 0);
}

/* ======================================================================================== */
/* Worker threads convert independently                                                     */
/* ======================================================================================== */

struct wk { unsigned long long ti_before, ti_after, ci_zero, ci_hkl, tid; ULONGLONG st; unsigned long long peb_tbl; int clean; };

static void wk_convert(void *p) {
    struct wk *w = (struct wk *)p;
    w->tid = GetCurrentThreadId();
    w->ti_before = gtc_teb_u64(TEB_Win32ThreadInfo);
    w->ci_zero = gtc_ci_all_zero();
    NtUserGetThreadState_pfn f = W(NtUserGetThreadState);
    w->st = f ? f(0xe) : 0;
    w->ti_after = gtc_teb_u64(TEB_Win32ThreadInfo);
    w->ci_hkl = gtc_ci_u64(CI_hKL);
    w->peb_tbl = gtc_peb_u64(PEB_GdiSharedHandleTable);
}
static void wk_nocall(void *p) {
    struct wk *w = (struct wk *)p;
    w->tid = GetCurrentThreadId();
    w->ti_before = gtc_teb_u64(TEB_Win32ThreadInfo);
    w->ci_zero = gtc_ci_all_zero();
    w->ti_after = gtc_teb_u64(TEB_Win32ThreadInfo);
}

SCEN(worker_fresh_thread_unconverted_after_main_converted) {
    convert_now();
    unsigned long long main_ti = gtc_teb_u64(TEB_Win32ThreadInfo);
    GC(main_ti != 0);
    struct wk w; memset(&w, 0, sizeof w);
    GC(gtc_thread(wk_nocall, &w));
    /* Win32ThreadInfo lives in the TEB, which is per-thread: the process being a win32k process
     * says nothing about a brand new thread. */
    GEQ(w.ti_before, 0);
    GEQ(w.ci_zero, 1);
    GEQ(w.ti_after, 0);
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), main_ti);
}

SCEN(worker_converts_independently) {
    convert_now();
    unsigned long long main_ti = gtc_teb_u64(TEB_Win32ThreadInfo);
    unsigned long long tbl = gtc_peb_u64(PEB_GdiSharedHandleTable);
    struct wk w; memset(&w, 0, sizeof w);
    GC(gtc_thread(wk_convert, &w));
    GEQ(w.ti_before, 0);
    GEQ(w.st, 1);
    GNE(w.ti_after, 0);
    GEQ(w.ti_after, w.tid);                 /* thread id marker, see header comment */
    GNE(w.ti_after, main_ti);
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), main_ti);   /* main thread untouched */
    GEQ(w.peb_tbl, tbl);                    /* process-wide table pointer does not move */
}

SCEN(worker_clientinfo_initialised_on_conversion) {
    convert_now();
    struct wk w; memset(&w, 0, sizeof w);
    GC(gtc_thread(wk_convert, &w));
    GEQ(w.ci_zero, 1);                      /* zero while unconverted */
    /* hKL is a process-agnostic default layout: identical for two threads of one process */
    GEQ(w.ci_hkl, gtc_ci_u64(CI_hKL));
}

#define NW 8
struct wk_multi { HANDLE go; unsigned long long ti[NW]; ULONGLONG st[NW]; int n; DWORD tid[NW]; HANDLE ready; LONG ready_cnt; };
static struct wk_multi g_mw;
static DWORD WINAPI wk_multi_thread(LPVOID p) {
    int i = (int)(INT_PTR)p;
    NtUserGetThreadState_pfn f = W(NtUserGetThreadState);
    g_mw.tid[i] = GetCurrentThreadId();
    g_mw.st[i] = f ? f(0xe) : 0;
    g_mw.ti[i] = gtc_teb_u64(TEB_Win32ThreadInfo);
    InterlockedIncrement(&g_mw.ready_cnt);
    WaitForSingleObject(g_mw.go, 8000);       /* stay alive so thread ids cannot be recycled */
    return 0;
}

SCEN(worker_many_threads_distinct_markers) {
    convert_now();
    HANDLE th[NW]; int created = 0;
    g_mw.go = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < NW; i++) { th[i] = CreateThread(NULL, 0, wk_multi_thread, (LPVOID)(INT_PTR)i, 0, NULL); if (th[i]) created++; }
    GEQ(created, NW);
    for (int spin = 0; spin < 400 && g_mw.ready_cnt < created; spin++) Sleep(10);
    GEQ(g_mw.ready_cnt, created);
    unsigned long long main_ti = gtc_teb_u64(TEB_Win32ThreadInfo);
    for (int i = 0; i < NW; i++) {
        GEQ(g_mw.st[i], 1);
        GEQ(g_mw.ti[i], g_mw.tid[i]);
        GNE(g_mw.ti[i], main_ti);
        for (int j = i + 1; j < NW; j++) GNE(g_mw.ti[i], g_mw.ti[j]);
    }
    SetEvent(g_mw.go);
    for (int i = 0; i < NW; i++) if (th[i]) { WaitForSingleObject(th[i], 5000); CloseHandle(th[i]); }
    CloseHandle(g_mw.go);
}

SCEN(worker_process_values_stable_across_conversions) {
    convert_now();
    unsigned long long tbl = gtc_peb_u64(PEB_GdiSharedHandleTable);
    unsigned long dcal = gtc_peb_u32(PEB_GdiDCAttributeList_off);
    for (int i = 0; i < 5; i++) {
        struct wk w; memset(&w, 0, sizeof w);
        GC(gtc_thread(wk_convert, &w));
        GEQ(w.ti_after, w.tid);
        GEQ(gtc_peb_u64(PEB_GdiSharedHandleTable), tbl);
        GEQ(gtc_peb_u32(PEB_GdiDCAttributeList_off), dcal);
        GEQ(gtc_peb_u64(PEB_KernelCallbackTable), gtc_stub_table());
    }
}

static void wk_first_in_process(void *p) {
    struct wk *w = (struct wk *)p;
    w->tid = GetCurrentThreadId();
    w->ti_before = gtc_teb_u64(TEB_Win32ThreadInfo);
    NtGdiFlush_pfn f = W(NtGdiFlush);
    if (f) f();
    w->ti_after = gtc_teb_u64(TEB_Win32ThreadInfo);
    w->peb_tbl = gtc_peb_u64(PEB_GdiSharedHandleTable);
}
SCEN(worker_can_be_first_gui_thread_in_process) {
    gtc_assert_pristine();
    struct wk w; memset(&w, 0, sizeof w);
    GC(gtc_thread(wk_first_in_process, &w));
    GEQ(w.ti_before, 0);
    GEQ(w.ti_after, w.tid);
    GNE(w.peb_tbl, 0);
    /* the process-level setup done by the worker is visible to the (still unconverted) main thread */
    GNE(gtc_peb_u64(PEB_GdiSharedHandleTable), 0);
    GEQ(gtc_peb_u32(PEB_GdiDCAttributeList_off), 0x14);
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), 0);
    GEQ(gtc_ci_all_zero(), 1);
}

SCEN(worker_exit_does_not_unconvert_process) {
    gtc_assert_pristine();
    struct wk w; memset(&w, 0, sizeof w);
    GC(gtc_thread(wk_first_in_process, &w));
    unsigned long long tbl = gtc_peb_u64(PEB_GdiSharedHandleTable);
    GNE(tbl, 0);
    /* worker is gone; the process-wide view stays mapped */
    unsigned char b;
    GC(k_try_read((void *)(ULONG_PTR)tbl, &b, 1));
    NtGdiFlush_pfn f = W(NtGdiFlush);
    if (f) f();
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), GetCurrentThreadId());
    GEQ(gtc_peb_u64(PEB_GdiSharedHandleTable), tbl);
}

/* ======================================================================================== */
/* Console-subsystem vs GUI-subsystem image: same rules                                      */
/* ======================================================================================== */

TEST(gui_thread_conversion_gui_subsystem_start_pristine) {
    gtc_parent_run_gui("start_win32threadinfo_null", 30000);
}
TEST(gui_thread_conversion_gui_subsystem_callback_table_null) {
    gtc_parent_run_gui("start_kernel_callback_table_null", 30000);
}
TEST(gui_thread_conversion_gui_subsystem_gdi_table_null) {
    gtc_parent_run_gui("start_gdi_shared_handle_table_null", 30000);
}
TEST(gui_thread_conversion_gui_subsystem_first_call) {
    gtc_parent_run_gui("first_call_thread_info_is_thread_id", 30000);
}
TEST(gui_thread_conversion_gui_subsystem_first_syscall_gdi) {
    gtc_parent_run_gui("first_syscall:6", 30000);
}
TEST(gui_thread_conversion_gui_subsystem_clientinfo) {
    gtc_parent_run_gui("clientinfo_expwinver_nonzero", 30000);
}
TEST(gui_thread_conversion_gui_subsystem_gdi_table_readonly) {
    gtc_parent_run_gui("first_call_gdi_table_is_readonly_mapped_view", 30000);
}
TEST(gui_thread_conversion_gui_subsystem_worker) {
    gtc_parent_run_gui("worker_converts_independently", 30000);
}
