/* Shell / desktop window identities, shell hook registration, system menu, job UI-handle grants,
 * process restriction exemption, BroadcastSystemMessage validation and power setting notifications. */
#include "imhm.h"

#define SENT 0x7e57u

UDECL(HWND, GetProgmanWindow, (void));
UDECL(HWND, GetTaskmanWindow, (void));
UDECL(BOOL, SetShellWindow, (HWND));

/* ---- DESKTOPINFO view ---------------------------------------------------------------------- */
/* user32!GetDesktopWindow / GetShellWindow / GetTaskmanWindow / GetProgmanWindow all read a kernel pointer
 * out of DESKTOPINFO (*(TEB+0x820)) at +0x08 / +0x18 / +0x20 / +0x28, add ulClientDelta (TEB+0x828) and
 * dereference the tagWND to get the HWND stored at its first qword. */
#define DI_DESKTOP 0x08
#define DI_SHELL   0x18
#define DI_TASKMAN 0x20
#define DI_PROGMAN 0x28
static bool di_hwnd(unsigned off, HWND *out, int *present) {
    void *di = NULL;
    uint64_t kp = 0, delta = 0, h = 0;
    *present = 0;
    if (!k_try_read_ptr((char *)k_teb() + 0x820, &di) || !di) return false;
    if (!k_try_read((char *)di + off, &kp, 8)) return false;
    if (!kp) { *out = NULL; return true; }
    if (!k_try_read((char *)k_teb() + 0x828, &delta, 8)) return false;
    if (!k_try_read((void *)(ULONG_PTR)(kp + delta), &h, 8)) return false;
    *present = 1;
    *out = (HWND)(ULONG_PTR)h;
    return true;
}

TEST(ime_hotkeys_misc_shell_desktop_window_matches_deskinfo) {
    if (!U(GetDesktopWindow)) { t_skip("no user32"); return; }
    HWND d = U(GetDesktopWindow)(), v; int p;
    if (!di_hwnd(DI_DESKTOP, &v, &p) || !p) { t_skip("DESKTOPINFO not readable"); return; }
    EXPECT_EQ(v, d);
}
TEST(ime_hotkeys_misc_shell_shell_window_matches_deskinfo) {
    if (!U(GetShellWindow)) { t_skip("no user32"); return; }
    HWND s = U(GetShellWindow)(), v = (HWND)1; int p;
    if (!di_hwnd(DI_SHELL, &v, &p)) { t_skip("DESKTOPINFO not readable"); return; }
    EXPECT_EQ(v, s);
}
TEST(ime_hotkeys_misc_shell_progman_window_matches_deskinfo) {
    if (!UN(GetProgmanWindow)) { t_skip("no GetProgmanWindow"); return; }
    HWND g = UN(GetProgmanWindow)(), v = (HWND)1; int p;
    if (!di_hwnd(DI_PROGMAN, &v, &p)) { t_skip("DESKTOPINFO not readable"); return; }
    EXPECT_EQ(v, g);
}
TEST(ime_hotkeys_misc_shell_taskman_window_matches_deskinfo) {
    if (!UN(GetTaskmanWindow)) { t_skip("no GetTaskmanWindow"); return; }
    HWND g = UN(GetTaskmanWindow)(), v = (HWND)1; int p;
    if (!di_hwnd(DI_TASKMAN, &v, &p)) { t_skip("DESKTOPINFO not readable"); return; }
    EXPECT_EQ(v, g);
}
TEST(ime_hotkeys_misc_shell_desktop_window_is_stable) {
    if (!U(GetDesktopWindow)) { t_skip("no user32"); return; }
    HWND a = U(GetDesktopWindow)();
    CHECK(a != NULL);
    for (int i = 0; i < 50; i++) if (U(GetDesktopWindow)() != a) { CHECKF(0, "changed at %d", i); break; }
}
TEST(ime_hotkeys_misc_shell_desktop_window_properties) {
    if (!U(GetDesktopWindow)) { t_skip("no user32"); return; }
    HWND d = U(GetDesktopWindow)();
    CHECK(U(IsWindow)(d));
    WCHAR cls[32] = L"";
    U(GetClassNameW)(d, cls, 32);
    CHECK(!lstrcmpW(cls, L"#32769"));        /* the desktop window class atom is 0x8001 */
    CHECK(U(GetParent)(d) == NULL);
    CHECK(U(GetAncestor)(d, GA_PARENT) == NULL);
    CHECK(U(IsWindowVisible)(d));
    DWORD pid = 0;
    CHECK(U(GetWindowThreadProcessId)(d, &pid) != 0);
    CHECK(pid != GetCurrentProcessId());     /* owned by the system, never by a user process */
}
TEST(ime_hotkeys_misc_shell_desktop_window_state_fnid) {
    if (!U(GetDesktopWindow)) { t_skip("no user32"); return; }
    HWND d = U(GetDesktopWindow)();
    void *p = k_wnd_ptr(d);
    if (!p) { t_skip("window structure not reachable"); return; }
    unsigned short fnid = 0;
    CHECK(k_try_read_u16((char *)p + 0x2a, &fnid));
    EXPECT_EQ(fnid & 0x2fff, 0x29d);    /* FNID_DESKTOP, tested by user32!GetWindowRect and friends */
}
TEST(ime_hotkeys_misc_shell_desktop_window_handle_entry) {
    if (!U(GetDesktopWindow)) { t_skip("no user32"); return; }
    struct k_user_entry e;
    if (!k_user_entry(U(GetDesktopWindow)(), &e)) { t_skip("handle table"); return; }
    EXPECT_EQ(e.type, K_TYPE_WINDOW);
    CHECK((e.flags & 1) == 0);
}
TEST(ime_hotkeys_misc_shell_desktop_window_is_not_our_thread) {
    if (!U(GetDesktopWindow)) { t_skip("no user32"); return; }
    DWORD pid = 0;
    DWORD tid = U(GetWindowThreadProcessId)(U(GetDesktopWindow)(), &pid);
    CHECK(tid != GetCurrentThreadId());
}
TEST(ime_hotkeys_misc_shell_desktop_is_parent_of_top_levels) {
    NEED_GUI(w);
    CHECK(U(GetAncestor)(w, GA_PARENT) == U(GetDesktopWindow)());
    CHECK(U(GetParent)(w) == NULL);
    im_kill(w);
}
TEST(ime_hotkeys_misc_shell_shell_window_not_ours_and_top_level) {
    HWND s = U(GetShellWindow)();
    if (!s) { t_skip("no shell window (no explorer on this desktop)"); return; }
    CHECK(U(IsWindow)(s));
    CHECK(U(GetAncestor)(s, GA_PARENT) == U(GetDesktopWindow)());
    DWORD pid = 0;
    U(GetWindowThreadProcessId)(s, &pid);
    CHECK(pid != GetCurrentProcessId());
    WCHAR cls[64] = L"";
    U(GetClassNameW)(s, cls, 64);
    t_note("shell window %p class %ls", s, cls);
}
TEST(ime_hotkeys_misc_shell_progman_is_shell_by_default) {
    HWND s = U(GetShellWindow)(), g = UN(GetProgmanWindow)();
    if (!s) { t_skip("no shell window"); return; }
    t_note("shell %p progman %p", s, g);
    if (g) {
        DWORD a = 0, b = 0;
        U(GetWindowThreadProcessId)(s, &a); U(GetWindowThreadProcessId)(g, &b);
        CHECK(a == b);        /* both are windows of the shell thread */
    }
}

/* Child: same handle values in another process of the same desktop. */
#define CC(c) do { if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
CHILD(imhm_ident) {
    int fails = 0;
    if (argc < 5 || !U(GetDesktopWindow)) return 0;
    HWND d = (HWND)(ULONG_PTR)strtoull(argv[1], NULL, 16);
    HWND s = (HWND)(ULONG_PTR)strtoull(argv[2], NULL, 16);
    HWND g = (HWND)(ULONG_PTR)strtoull(argv[3], NULL, 16);
    HWND t = (HWND)(ULONG_PTR)strtoull(argv[4], NULL, 16);
    CC(U(GetDesktopWindow)() == d);
    CC(U(GetShellWindow)() == s);
    CC(UN(GetProgmanWindow)() == g);
    CC(UN(GetTaskmanWindow)() == t);
    CC(U(IsWindow)(d));
    return fails;
}
TEST(ime_hotkeys_misc_shell_identity_same_in_other_process) {
    if (t_is_wine()) { t_skip("child processes need a real desktop connection"); return; }
    if (!U(GetDesktopWindow) || !UN(GetProgmanWindow) || !UN(GetTaskmanWindow)) { t_skip("missing export"); return; }
    char args[160];
    snprintf(args, sizeof args, "%llx %llx %llx %llx", (unsigned long long)(ULONG_PTR)U(GetDesktopWindow)(),
             (unsigned long long)(ULONG_PTR)U(GetShellWindow)(), (unsigned long long)(ULONG_PTR)UN(GetProgmanWindow)(),
             (unsigned long long)(ULONG_PTR)UN(GetTaskmanWindow)());
    CHECK(t_run_child("imhm_ident", args, 30000) == 0);
}

/* ---- NtUserSetShellWindowEx ------------------------------------------------------------------ */
#define NEED_SSW() do { if (!UN(NtUserSetShellWindowEx) || !U(GetShellWindow)) { t_skip("missing stub"); return; } } while (0)
TEST(ime_hotkeys_misc_ssw_bogus_window) {
    NEED_SSW();
    SET_ERR(SENT);
    EXPECT_FAIL(UN(NtUserSetShellWindowEx)(im_bogus_hwnd(), im_bogus_hwnd()) == 0, ERROR_INVALID_WINDOW_HANDLE);
}
TEST(ime_hotkeys_misc_ssw_null_window) {
    NEED_SSW();
    EXPECT_EQ(UN(NtUserSetShellWindowEx)(NULL, NULL), 0);
}
TEST(ime_hotkeys_misc_ssw_destroyed_window) {
    NEED_SSW(); NEED_GUI(w);
    im_kill(w);
    EXPECT_EQ(UN(NtUserSetShellWindowEx)(w, w), 0);
}
TEST(ime_hotkeys_misc_ssw_desktop_window_refused_silently) {
    NEED_SSW();
    HWND d = U(GetDesktopWindow)();
    if (!d) { t_skip("no desktop"); return; }
    HWND before = U(GetShellWindow)();
    /* NtUserSetShellWindowEx (win32kfull 0x81da0) leaves via the early exit with return 0 when the fnid
     * (masked with 0xffff2fff) is 0x29d (desktop) or 0x29f; no UserSetLastError is on that path, so the
     * last error is not asserted here (only the FALSE result and unchanged shell). */
    SET_ERR(SENT);
    EXPECT_EQ(UN(NtUserSetShellWindowEx)(d, d), 0);
    EXPECT_EQ(U(GetShellWindow)(), before);
}
TEST(ime_hotkeys_misc_ssw_already_has_shell_denied) {
    NEED_SSW(); NEED_GUI(w);
    HWND before = U(GetShellWindow)();
    if (!before) { t_skip("no shell window: setting one would change desktop state"); im_kill(w); return; }
    SET_ERR(SENT);
    BOOL r = UN(NtUserSetShellWindowEx)(w, w);
    /* Either the owner-process access test in NtUserSetShellWindowEx or the shell-window-already-set check ends in UserSetLastError(5); both give ERROR_ACCESS_DENIED. The
     * test needs a running shell (skipped above when GetShellWindow is NULL). */
    EXPECT_FAIL(r == 0, ERROR_ACCESS_DENIED);
    EXPECT_EQ(U(GetShellWindow)(), before);
    im_kill(w);
}
TEST(ime_hotkeys_misc_ssw_user32_setshellwindow_denied) {
    NEED_SSW(); NEED_GUI(w);
    HWND before = U(GetShellWindow)();
    if (!before || !UN(SetShellWindow)) { t_skip("no shell window or export"); im_kill(w); return; }
    SET_ERR(SENT);
    EXPECT_FAIL(UN(SetShellWindow)(w) == 0, ERROR_ACCESS_DENIED);    /* SetShellWindow(h) == NtUserSetShellWindowEx(h, h) */
    EXPECT_EQ(U(GetShellWindow)(), before);
    im_kill(w);
}
TEST(ime_hotkeys_misc_ssw_second_window_param_invalid) {
    NEED_SSW(); NEED_GUI(w);
    HWND before = U(GetShellWindow)();
    if (!before) { t_skip("no shell window"); im_kill(w); return; }
    EXPECT_EQ(UN(NtUserSetShellWindowEx)(w, im_bogus_hwnd()), 0);
    EXPECT_EQ(U(GetShellWindow)(), before);
    im_kill(w);
}

/* ---- shell hook registration ------------------------------------------------------------------ */
#define NEED_SH() do { if (!U(RegisterShellHookWindow) || !U(DeregisterShellHookWindow)) { t_skip("no shell hook exports"); return; } } while (0)
TEST(ime_hotkeys_misc_shh_register_deregister) {
    NEED_SH(); NEED_GUI(w);
    SET_ERR(SENT);
    BOOL r = U(RegisterShellHookWindow)(w);
    if (!r) t_note("RegisterShellHookWindow failed, err %lu", GetLastError());
    CHECK(r);
    EXPECT_EQ(U(DeregisterShellHookWindow)(w), 1);
    im_kill(w);
}
TEST(ime_hotkeys_misc_shh_register_twice) {
    NEED_SH(); NEED_GUI(w);
    BOOL a = U(RegisterShellHookWindow)(w);
    BOOL b = U(RegisterShellHookWindow)(w);
    t_note("second RegisterShellHookWindow -> %d (first %d)", b, a);
    U(DeregisterShellHookWindow)(w);
    U(DeregisterShellHookWindow)(w);
    im_kill(w);
}
TEST(ime_hotkeys_misc_shh_deregister_unregistered_window_succeeds) {
    NEED_SH(); NEED_GUI(w);
    /* _DeregisterShellHookWindow returns 1 for any window that belongs to a desktop */
    EXPECT_EQ(U(DeregisterShellHookWindow)(w), 1);
    im_kill(w);
}
TEST(ime_hotkeys_misc_shh_bogus_window) {
    NEED_SH();
    SET_ERR(SENT);
    EXPECT_FAIL(U(RegisterShellHookWindow)(im_bogus_hwnd()) == 0, ERROR_INVALID_WINDOW_HANDLE);   /* NtUserCallHwnd: ValidateHwnd */
    SET_ERR(SENT);
    EXPECT_FAIL(U(DeregisterShellHookWindow)(im_bogus_hwnd()) == 0, ERROR_INVALID_WINDOW_HANDLE);
}
TEST(ime_hotkeys_misc_shh_null_window) {
    NEED_SH();
    EXPECT_EQ(U(RegisterShellHookWindow)(NULL), 0);
    EXPECT_EQ(U(DeregisterShellHookWindow)(NULL), 0);
}
TEST(ime_hotkeys_misc_shh_destroyed_window) {
    NEED_SH(); NEED_GUI(w);
    CHECK(U(RegisterShellHookWindow)(w));
    im_kill(w);
    EXPECT_EQ(U(DeregisterShellHookWindow)(w), 0);
}
TEST(ime_hotkeys_misc_shh_other_process_window) {
    NEED_SH();
    HWND s = U(GetShellWindow)();
    if (!s) { t_skip("no shell window"); return; }
    DWORD pid = 0; U(GetWindowThreadProcessId)(s, &pid);
    if (pid == GetCurrentProcessId()) { t_skip("shell is ours"); return; }
    /* registering a foreign window would add it to our desktop's list; just record what the kernel does */
    BOOL r = U(DeregisterShellHookWindow)(s);    /* removal is harmless: never registered through us */
    t_note("DeregisterShellHookWindow(foreign) -> %d", r);
}
TEST(ime_hotkeys_misc_shh_child_window_note) {
    NEED_SH(); NEED_GUI(w);
    HWND c = im_mkwnd(WS_CHILD, 0, w);
    if (!c) { im_kill(w); t_skip("child"); return; }
    SET_ERR(SENT);
    BOOL r = U(RegisterShellHookWindow)(c);
    t_note("RegisterShellHookWindow(child) -> %d err %lu", r, r ? 0 : GetLastError());
    U(DeregisterShellHookWindow)(c);
    im_kill(w);
}
TEST(ime_hotkeys_misc_shh_destroy_cleans_up) {
    NEED_SH();
    for (int i = 0; i < 20; i++) {
        HWND w = im_mkpopup();
        if (!w) { t_skip("window"); return; }
        U(RegisterShellHookWindow)(w);
        im_kill(w);          /* window destruction drops the VWPL entry; nothing to leak or dangle */
    }
    NEED_GUI(w2);
    CHECK(U(RegisterShellHookWindow)(w2));
    U(DeregisterShellHookWindow)(w2);
    im_kill(w2);
}

/* ---- GetSystemMenu ------------------------------------------------------------------------------ */
#define NEED_SYSMENU() do { if (!UN(NtUserGetSystemMenu) || !U(IsMenu)) { t_skip("missing stub"); return; } } while (0)
static HWND mk_sys(void) { return im_mkwnd(WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, 0, NULL); }
#define NEED_SYS(w) NEED_SYSMENU(); HWND w = mk_sys(); if (!(w)) { t_skip("cannot create window"); return; }
TEST(ime_hotkeys_misc_sysmenu_returns_menu) {
    NEED_SYS(w);
    HMENU m = UN(NtUserGetSystemMenu)(w, FALSE);
    CHECK(m != NULL);
    CHECK(U(IsMenu)(m));
    im_kill(w);
}
TEST(ime_hotkeys_misc_sysmenu_handle_type_is_menu) {
    NEED_SYS(w);
    HMENU m = UN(NtUserGetSystemMenu)(w, FALSE);
    struct k_user_entry e;
    if (m && k_user_entry(m, &e)) EXPECT_EQ(e.type, IM_TYPE_MENU);
    else t_skip("no menu or handle table");
    im_kill(w);
}
TEST(ime_hotkeys_misc_sysmenu_same_handle_each_call) {
    NEED_SYS(w);
    HMENU a = UN(NtUserGetSystemMenu)(w, FALSE), b = UN(NtUserGetSystemMenu)(w, FALSE);
    EXPECT_EQ(a, b);      /* the per-window copy is cached in pwnd->spmenuSys */
    im_kill(w);
}
TEST(ime_hotkeys_misc_sysmenu_contains_close) {
    NEED_SYS(w);
    HMENU m = UN(NtUserGetSystemMenu)(w, FALSE);
    if (!m) { im_kill(w); t_skip("no menu"); return; }
    CHECK(U(GetMenuState)(m, SC_CLOSE, MF_BYCOMMAND) != (UINT)-1);
    CHECK(U(GetMenuState)(m, SC_MOVE, MF_BYCOMMAND) != (UINT)-1);
    CHECK(U(GetMenuState)(m, SC_SIZE, MF_BYCOMMAND) != (UINT)-1);     /* WS_THICKFRAME keeps Size */
    CHECK(U(GetMenuItemCount)(m) >= 3);
    im_kill(w);
}
TEST(ime_hotkeys_misc_sysmenu_not_the_window_menu) {
    NEED_SYS(w);
    HMENU m = UN(NtUserGetSystemMenu)(w, FALSE);
    CHECK(U(GetMenu)(w) == NULL);
    CHECK(m != U(GetMenu)(w));
    im_kill(w);
}
TEST(ime_hotkeys_misc_sysmenu_without_style_is_null) {
    NEED_SYSMENU(); NEED_GUI(w);
    EXPECT_EQ(UN(NtUserGetSystemMenu)(w, FALSE), NULL);    /* WS_SYSMENU (0x80000) bit required to load the menu */
    im_kill(w);
}
TEST(ime_hotkeys_misc_sysmenu_style_added_later) {
    NEED_SYSMENU(); NEED_GUI(w);
    LONG st = U(GetWindowLongW)(w, GWL_STYLE);
    U(SetWindowLongW)(w, GWL_STYLE, st | WS_SYSMENU | WS_CAPTION);
    HMENU m = UN(NtUserGetSystemMenu)(w, FALSE);
    t_note("system menu after adding WS_SYSMENU: %p", m);
    if (m) CHECK(U(IsMenu)(m));
    im_kill(w);
}
TEST(ime_hotkeys_misc_sysmenu_revert_returns_null) {
    NEED_SYS(w);
    CHECK(UN(NtUserGetSystemMenu)(w, FALSE) != NULL);
    EXPECT_EQ(UN(NtUserGetSystemMenu)(w, TRUE), NULL);     /* bRevert: window goes back to the shared menu */
    im_kill(w);
}
TEST(ime_hotkeys_misc_sysmenu_revert_destroys_copy) {
    NEED_SYS(w);
    HMENU m = UN(NtUserGetSystemMenu)(w, FALSE);
    if (!m) { im_kill(w); t_skip("no menu"); return; }
    UN(NtUserGetSystemMenu)(w, TRUE);
    CHECK(!U(IsMenu)(m));
    HMENU n = UN(NtUserGetSystemMenu)(w, FALSE);
    CHECK(n != NULL);
    CHECK(U(IsMenu)(n));
    im_kill(w);
}
TEST(ime_hotkeys_misc_sysmenu_destroyed_with_window) {
    NEED_SYS(w);
    HMENU m = UN(NtUserGetSystemMenu)(w, FALSE);
    if (!m) { im_kill(w); t_skip("no menu"); return; }
    im_kill(w);
    t_note("system menu %p after window destruction: IsMenu %d", m, U(IsMenu)(m));   /* owned by the window (spmenuSys) */
}
TEST(ime_hotkeys_misc_sysmenu_distinct_per_window) {
    NEED_SYS(w);
    HWND w2 = mk_sys();
    if (!w2) { im_kill(w); t_skip("second window"); return; }
    HMENU a = UN(NtUserGetSystemMenu)(w, FALSE), b = UN(NtUserGetSystemMenu)(w2, FALSE);
    CHECK(a != NULL && b != NULL);
    CHECK(a != b);
    im_kill(w); im_kill(w2);
}
TEST(ime_hotkeys_misc_sysmenu_bogus_window) {
    NEED_SYSMENU();
    SET_ERR(SENT);
    EXPECT_EQ(UN(NtUserGetSystemMenu)(im_bogus_hwnd(), FALSE), NULL);
    EXPECT_EQ(UN(NtUserGetSystemMenu)(NULL, FALSE), NULL);
    EXPECT_EQ(UN(NtUserGetSystemMenu)(im_bogus_hwnd(), TRUE), NULL);
}
TEST(ime_hotkeys_misc_sysmenu_user32_matches_syscall) {
    NEED_SYS(w);
    if (!U(GetSystemMenu)) { im_kill(w); t_skip("no export"); return; }
    HMENU a = U(GetSystemMenu)(w, FALSE), b = UN(NtUserGetSystemMenu)(w, FALSE);
    EXPECT_EQ(a, b);
    im_kill(w);
}
TEST(ime_hotkeys_misc_sysmenu_no_thickframe_has_no_size) {
    NEED_SYSMENU();
    HWND w = im_mkwnd(WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, NULL);
    if (!w) { t_skip("window"); return; }
    HMENU m = UN(NtUserGetSystemMenu)(w, FALSE);
    if (m) {
        UINT st = U(GetMenuState)(m, SC_SIZE, MF_BYCOMMAND);
        t_note("SC_SIZE state without WS_THICKFRAME: 0x%x", st);
        CHECK(U(GetMenuState)(m, SC_CLOSE, MF_BYCOMMAND) != (UINT)-1);
    }
    im_kill(w);
}

/* ---- UserHandleGrantAccess -------------------------------------------------------------------- */
#define NEED_GRANT() do { if (!UN(NtUserUserHandleGrantAccess)) { t_skip("missing stub"); return; } } while (0)
TEST(ime_hotkeys_misc_grant_null_job) {
    NEED_GRANT(); NEED_GUI(w);
    SET_ERR(SENT);
    EXPECT_FAIL(UN(NtUserUserHandleGrantAccess)(w, NULL, TRUE) == 0, ERROR_INVALID_PARAMETER);   /* ObReferenceObjectByHandle fails */
    im_kill(w);
}
TEST(ime_hotkeys_misc_grant_bogus_job_handle) {
    NEED_GRANT(); NEED_GUI(w);
    SET_ERR(SENT);
    EXPECT_FAIL(UN(NtUserUserHandleGrantAccess)(w, (HANDLE)(ULONG_PTR)0x7fff0, TRUE) == 0, ERROR_INVALID_PARAMETER);
    SET_ERR(SENT);
    EXPECT_FAIL(UN(NtUserUserHandleGrantAccess)(w, INVALID_HANDLE_VALUE, FALSE) == 0, ERROR_INVALID_PARAMETER);
    im_kill(w);
}
TEST(ime_hotkeys_misc_grant_handle_of_wrong_object_type) {
    NEED_GRANT(); NEED_GUI(w);
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    SET_ERR(SENT);
    EXPECT_FAIL(UN(NtUserUserHandleGrantAccess)(w, ev, TRUE) == 0, ERROR_INVALID_PARAMETER);    /* PsJobType required */
    SET_ERR(SENT);
    EXPECT_FAIL(UN(NtUserUserHandleGrantAccess)(w, GetCurrentProcess(), TRUE) == 0, ERROR_INVALID_PARAMETER);
    CloseHandle(ev);
    im_kill(w);
}
TEST(ime_hotkeys_misc_grant_job_without_ui_restrictions) {
    NEED_GRANT(); NEED_GUI(w);
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (!job) { im_kill(w); t_skip("CreateJobObject failed"); return; }
    SET_ERR(SENT);
    /* PsGetJobUIRestrictionsClass(job) == 0 -> straight to ERROR_INVALID_PARAMETER */
    EXPECT_FAIL(UN(NtUserUserHandleGrantAccess)(w, job, TRUE) == 0, ERROR_INVALID_PARAMETER);
    SET_ERR(SENT);
    EXPECT_FAIL(UN(NtUserUserHandleGrantAccess)(w, job, FALSE) == 0, ERROR_INVALID_PARAMETER);
    CloseHandle(job);
    im_kill(w);
}
TEST(ime_hotkeys_misc_grant_job_with_ui_restrictions_accepts_grant_and_revoke) {
    NEED_GRANT(); NEED_GUI(w);
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (!job) { im_kill(w); t_skip("CreateJobObject failed"); return; }
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui;
    ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_HANDLES;
    if (!SetInformationJobObject(job, JobObjectBasicUIRestrictions, &ui, sizeof ui)) {
        t_note("cannot set UI restrictions, err %lu", GetLastError());
        CloseHandle(job); im_kill(w); t_skip("cannot set the job's UI restrictions"); return;
    }
    /* Once the job carries UI restrictions the grant works even though no process was assigned to it (on Windows 10 the job
     * is entered into the kernel's list when the restriction is set); granting twice and revoking are accepted as well. */
    SET_ERR(SENT);
    CHECK(UN(NtUserUserHandleGrantAccess)(w, job, TRUE) != 0);
    EXPECT_ERR(SENT);
    CHECK(UN(NtUserUserHandleGrantAccess)(w, job, TRUE) != 0);
    CHECK(UN(NtUserUserHandleGrantAccess)(w, job, FALSE) != 0);
    CHECK(UN(NtUserUserHandleGrantAccess)(w, job, FALSE) != 0);
    CloseHandle(job);
    im_kill(w);
}
TEST(ime_hotkeys_misc_grant_job_handle_without_set_attributes_access) {
    NEED_GRANT(); NEED_GUI(w);
    HANDLE job = CreateJobObjectW(NULL, NULL), ro = NULL;
    if (!job || !DuplicateHandle(GetCurrentProcess(), job, GetCurrentProcess(), &ro, JOB_OBJECT_QUERY, FALSE, 0)) {
        if (job) CloseHandle(job);
        im_kill(w); t_skip("setup"); return;
    }
    SET_ERR(SENT);
    EXPECT_FAIL(UN(NtUserUserHandleGrantAccess)(w, ro, TRUE) == 0, ERROR_INVALID_PARAMETER);   /* access 2 (SET_ATTRIBUTES) requested */
    CloseHandle(ro); CloseHandle(job);
    im_kill(w);
}
TEST(ime_hotkeys_misc_grant_user32_path) {
    NEED_GRANT(); NEED_GUI(w);
    if (!U(UserHandleGrantAccess)) { im_kill(w); t_skip("no user32 export"); return; }
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (!job) { im_kill(w); t_skip("job"); return; }
    SET_ERR(SENT);
    EXPECT_FAIL(U(UserHandleGrantAccess)(w, job, TRUE) == 0, ERROR_INVALID_PARAMETER);
    CloseHandle(job);
    im_kill(w);
}

/* ---- restriction exemption (process wide flag: child only) ----------------------------------- */
CHILD(imhm_restriction_exemption) {
    int fails = 0;
    if (!UN(NtUserSetProcessRestrictionExemption) || !U(GetDesktopWindow)) { printf("NOTE child: stub missing\n"); return 0; }
    U(GetDesktopWindow)();
    SetLastError(SENT);
    BOOL r = UN(NtUserSetProcessRestrictionExemption)(1);
    DWORD e = GetLastError();
    if (r) {
        printf("NOTE exemption granted to this process (dev license present)\n");
        CC(UN(NtUserSetProcessRestrictionExemption)(0) == 1);      /* restore: clears bit 8 of the process flags */
    } else {
        CC(e == ERROR_ACCESS_DENIED);       /* ExQueryFastCacheDevLicense false -> UserSetLastError(5) */
        SetLastError(SENT);
        CC(UN(NtUserSetProcessRestrictionExemption)(0) == 0);
        CC(GetLastError() == ERROR_ACCESS_DENIED);
    }
    return fails;
}
TEST(ime_hotkeys_misc_restr_exemption_child) {
    if (t_is_wine()) { t_skip("child processes need a real desktop connection"); return; }
    CHECK(t_run_child("imhm_restriction_exemption", "", 30000) == 0);
}

/* ---- BroadcastSystemMessage validation (client side worker) ------------------------------------ */
#define BSM(flags, rec, msg, w, l) U(BroadcastSystemMessageW)((flags), (rec), (msg), (w), (l))
#define NEED_BSM() do { if (!U(BroadcastSystemMessageW) || !U(BroadcastSystemMessageExW) || !U(RegisterWindowMessageW)) { t_skip("no BroadcastSystemMessage"); return; } } while (0)
static UINT bsm_msg(void) {
    static UINT m;
    if (!m) { WCHAR n[64]; U(wsprintfW)(n, L"win32kt_bsm_%lu", GetCurrentProcessId()); m = U(RegisterWindowMessageW)(n); }
    return m;
}
TEST(ime_hotkeys_misc_bsm_message_above_17_bits) {
    NEED_BSM();
    static const UINT msgs[] = { 0x20000, 0x20001, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof msgs / sizeof msgs[0]; i++) {
        DWORD rec = BSM_APPLICATIONS;
        SET_ERR(SENT);
        long r = BSM(0, &rec, msgs[i], 0, 0);
        CHECKF(r == 0, "msg 0x%x", msgs[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "msg 0x%x err %lu", msgs[i], GetLastError());   /* (msg & 0xfffe0000) */
    }
}
TEST(ime_hotkeys_misc_bsm_user_message_range_rejected) {
    NEED_BSM();
    static const UINT msgs[] = { WM_USER, WM_USER + 1, 0x7fff, 0xafff, 0xbfff };
    for (unsigned i = 0; i < sizeof msgs / sizeof msgs[0]; i++) {
        DWORD rec = BSM_APPLICATIONS;
        SET_ERR(SENT);
        long r = BSM(0, &rec, msgs[i], 0, 0);
        CHECKF(r == 0, "msg 0x%x", msgs[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "msg 0x%x err %lu", msgs[i], GetLastError());   /* 0x400..0xbfff */
    }
}
TEST(ime_hotkeys_misc_bsm_invalid_flag_bits) {
    NEED_BSM();
    static const DWORD fl[] = { 0x800, 0x1000, 0x80000000, 0xfffff800 };
    for (unsigned i = 0; i < sizeof fl / sizeof fl[0]; i++) {
        DWORD rec = BSM_APPLICATIONS;
        SET_ERR(SENT);
        long r = BSM(fl[i], &rec, bsm_msg(), 0, 0);
        CHECKF(r == 0, "flags 0x%lx", fl[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "flags 0x%lx err %lu", fl[i], GetLastError());   /* (flags & 0xfffff800) */
    }
}
TEST(ime_hotkeys_misc_bsm_hdesk_and_luid_flags_need_info) {
    NEED_BSM();
    DWORD rec = BSM_APPLICATIONS;
    SET_ERR(SENT);
    EXPECT_EQ(BSM(BSF_RETURNHDESK, &rec, bsm_msg(), 0, 0), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);    /* (flags & 0x600) requires a BSMINFO */
    SET_ERR(SENT);
    EXPECT_EQ(BSM(BSF_LUID, &rec, bsm_msg(), 0, 0), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(ime_hotkeys_misc_bsm_info_size_checked) {
    NEED_BSM();
    BSMINFO info;
    DWORD rec = BSM_APPLICATIONS;
    static const UINT sizes[] = { 0, 4, 0x1f, 0x21, 0x28, 0xffffffff };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        memset(&info, 0, sizeof info);
        info.cbSize = sizes[i];
        SET_ERR(SENT);
        long r = U(BroadcastSystemMessageExW)(0, &rec, bsm_msg(), 0, 0, &info);
        CHECKF(r == 0, "cbSize 0x%x", sizes[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "cbSize 0x%x err %lu", sizes[i], GetLastError());   /* cbSize != 0x20 */
    }
    EXPECT_EQ(sizeof(BSMINFO), 0x20);
}
TEST(ime_hotkeys_misc_bsm_invalid_recipient_bits) {
    NEED_BSM();
    static const DWORD recs[] = { 0x20, 0x40, 0x100, 0x80000000, 0xffffffe0, 0x28 };
    for (unsigned i = 0; i < sizeof recs / sizeof recs[0]; i++) {
        DWORD rec = recs[i];
        SET_ERR(SENT);
        long r = BSM(0, &rec, bsm_msg(), 0, 0);
        CHECKF(r == 0, "recipients 0x%lx", recs[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "recipients 0x%lx err %lu", recs[i], GetLastError());   /* (rec & 0xffffffe0) */
    }
}
TEST(ime_hotkeys_misc_bsm_non_application_recipients_return_minus_one) {
    NEED_BSM();
    static const DWORD recs[] = { BSM_VXDS, BSM_NETDRIVER, 4 /* BSM_INSTALLABLEDRIVERS */, BSM_VXDS | BSM_NETDRIVER };
    for (unsigned i = 0; i < sizeof recs / sizeof recs[0]; i++) {
        DWORD rec = recs[i];
        long r = BSM(0, &rec, bsm_msg(), 0, 0);
        CHECKF(r == -1, "recipients 0x%lx -> %ld", recs[i], r);    /* no BSM_APPLICATIONS bit: nothing is sent, -1 is returned */
        CHECKF(rec == recs[i], "recipients word modified to 0x%lx", rec);
    }
}
TEST(ime_hotkeys_misc_bsm_a_and_w_agree_on_validation) {
    NEED_BSM();
    if (!U(BroadcastSystemMessageA)) { t_skip("no A export"); return; }
    DWORD rec = 0x20;
    SET_ERR(SENT);
    EXPECT_EQ(U(BroadcastSystemMessageA)(0, &rec, 0x20000, 0, 0), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

static volatile LONG g_bsm_hits; static volatile LONG g_bsm_deny; static UINT g_bsm_id;
static LRESULT CALLBACK bsm_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_bsm_id && m == g_bsm_id) { InterlockedIncrement(&g_bsm_hits); return g_bsm_deny ? BROADCAST_QUERY_DENY : TRUE; }
    return U(DefWindowProcW)(h, m, w, l);
}
/* The window lives on its own thread with a message loop: the thread calling BroadcastSystemMessage is blocked inside it
 * and cannot receive the broadcast itself. */
static HANDLE g_bsm_stop, g_bsm_up; static HWND g_bsm_hwnd;
static DWORD WINAPI bsm_owner(LPVOID p) {
    g_bsm_hwnd = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"win32kt_imhm_bsm", L"bsm", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, GetModuleHandleW(NULL), NULL);
    SetEvent(g_bsm_up);
    while (WaitForSingleObject(g_bsm_stop, 0) != WAIT_OBJECT_0) {
        MSG m; while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
        U(MsgWaitForMultipleObjects)(1, &g_bsm_stop, FALSE, 10, QS_ALLINPUT);
    }
    if (g_bsm_hwnd) U(DestroyWindow)(g_bsm_hwnd);
    return 0;
}
TEST(ime_hotkeys_misc_bsm_query_reaches_own_window_and_can_be_denied) {
    NEED_BSM();
    if (!U(RegisterClassExW) || !U(CreateWindowExW)) { t_skip("no user32"); return; }
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.lpfnWndProc = bsm_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"win32kt_imhm_bsm";
    if (!U(RegisterClassExW)(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { t_skip("class registration failed"); return; }
    g_bsm_stop = CreateEventW(NULL, TRUE, FALSE, NULL); g_bsm_up = CreateEventW(NULL, TRUE, FALSE, NULL); g_bsm_hwnd = NULL;
    HANDLE t = CreateThread(NULL, 0, bsm_owner, NULL, 0, NULL);
    if (!t || WaitForSingleObject(g_bsm_up, 5000) != WAIT_OBJECT_0 || !g_bsm_hwnd) { SetEvent(g_bsm_stop); if (t) { WaitForSingleObject(t, 3000); CloseHandle(t); } CloseHandle(g_bsm_stop); CloseHandle(g_bsm_up); t_skip("window"); return; }
    g_bsm_id = bsm_msg(); g_bsm_hits = 0; g_bsm_deny = 1;
    DWORD rec = BSM_APPLICATIONS;
    /* BSF_QUERY: every top-level window is asked; a BROADCAST_QUERY_DENY reply makes the call return 0.
     * BSF_FORCEIFHUNG keeps the delivery to windows whose thread the system considers hung (a broadcast without it was seen
     * to deliver nothing at all). */
    long r = BSM(BSF_QUERY | BSF_FORCEIFHUNG, &rec, g_bsm_id, 0, 0);
    t_note("broadcast query denied -> %ld, hits %ld", r, (long)g_bsm_hits);
    CHECK(g_bsm_hits >= 1);
    EXPECT_EQ(r, 0);
    g_bsm_deny = 0; g_bsm_hits = 0;
    r = BSM(BSF_QUERY | BSF_FORCEIFHUNG, &rec, g_bsm_id, 0, 0);
    CHECKF(r > 0, "accepted query returned %ld", r);     /* documented as "a positive value" (1 elevated, 0x20 unprivileged) */
    CHECK(g_bsm_hits >= 1);
    SetEvent(g_bsm_stop); WaitForSingleObject(t, 3000); CloseHandle(t);
    CloseHandle(g_bsm_stop); CloseHandle(g_bsm_up);
    g_bsm_id = 0;
}

/* ---- power setting notifications (user32 -> umpo, no win32k) ------------------------------------ */
static const GUID kGuidMonitorOn = { 0x02731015, 0x4510, 0x4526, { 0x99, 0xE6, 0xE5, 0xA1, 0x7E, 0xBD, 0x1A, 0xEA } };
TEST(ime_hotkeys_misc_pwr_register_unregister_window) {
    if (!U(RegisterPowerSettingNotification) || !U(UnregisterPowerSettingNotification)) { t_skip("missing export"); return; }
    NEED_GUI(w);
    SET_ERR(SENT);
    HPOWERNOTIFY h = U(RegisterPowerSettingNotification)(w, &kGuidMonitorOn, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!h) { t_note("RegisterPowerSettingNotification failed, err %lu", GetLastError()); im_kill(w); t_skip("not available"); return; }
    CHECK(U(UnregisterPowerSettingNotification)(h));
    im_kill(w);
}
TEST(ime_hotkeys_misc_pwr_register_bad_flags) {
    if (!U(RegisterPowerSettingNotification)) { t_skip("missing export"); return; }
    NEED_GUI(w);
    SET_ERR(SENT);
    HPOWERNOTIFY h = U(RegisterPowerSettingNotification)(w, &kGuidMonitorOn, 0x80);
    t_note("flags 0x80 -> %p err %lu", h, h ? 0 : GetLastError());
    if (h) U(UnregisterPowerSettingNotification)(h);
    im_kill(w);
}
TEST(ime_hotkeys_misc_pwr_handles_are_distinct) {
    if (!U(RegisterPowerSettingNotification) || !U(UnregisterPowerSettingNotification)) { t_skip("missing export"); return; }
    NEED_GUI(w);
    HPOWERNOTIFY a = U(RegisterPowerSettingNotification)(w, &kGuidMonitorOn, DEVICE_NOTIFY_WINDOW_HANDLE);
    HPOWERNOTIFY b = U(RegisterPowerSettingNotification)(w, &kGuidMonitorOn, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!a || !b) { t_note("registration failed (%p %p)", a, b); if (a) U(UnregisterPowerSettingNotification)(a); if (b) U(UnregisterPowerSettingNotification)(b); im_kill(w); t_skip("not available"); return; }
    CHECK(a != b);
    CHECK(U(UnregisterPowerSettingNotification)(a));
    CHECK(U(UnregisterPowerSettingNotification)(b));
    im_kill(w);
}
