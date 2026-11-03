/* NtUserCallNoParam per-code semantics (win32kfull!NtUserCallNoParam; table entry names are the
 * public APIs whose user32 stub emits the code). Codes 0 and 1 return *result (the HMENU is read out
 * of the new menu object's head), every other code returns the raw function result. */
#include "mx.h"

/* ---- 0x00 CreateMenu / 0x01 CreatePopupMenu --------------------------------------------------- */
static void create_menu_case(DWORD code) {
    NEED_RAW(NtUserCallNoParam);
    HMENU m1 = (HMENU)NP(code), m2 = (HMENU)NP(code);
    CHECKF(m1 != NULL && m2 != NULL, "code 0x%lx returned %p / %p", (unsigned long)code, m1, m2);
    if (!m1 || !m2) { if (m1) U(DestroyMenu)(m1); if (m2) U(DestroyMenu)(m2); return; }
    CHECK(m1 != m2);
    CHECK(U(IsMenu)(m1));
    CHECK(U(IsMenu)(m2));
 /* the kernel returned the handle stored in the object head, not the object pointer:
     * a handle's index (low word) is inside the USER handle table and its uniq (high word) is nonzero */
    CHECK(((ULONG_PTR)m1 >> 16) != 0);
    CHECK((((ULONG_PTR)m1) & 0xffff) != 0);
    CHECK(U(GetMenuItemCount)(m1) == 0);
    struct k_user_entry e;
    if (k_shared_info_ptr() && k_user_entry((HANDLE)m1, &e)) {
        CHECKF(e.type == 2, "handle-table type %u, want TYPE_MENU (2)", e.type);   /* HMENU is USER type 2 */
        CHECK((e.flags & 1) == 0);
        CHECKF(e.uniq == (WORD)((ULONG_PTR)m1 >> 16), "entry uniq 0x%x handle 0x%llx", e.uniq, (unsigned long long)(ULONG_PTR)m1);
    } else t_note("USER handle entry not readable");
    CHECK(U(DestroyMenu)(m1));
    CHECK(!U(IsMenu)(m1));
    SetLastError(0);
    CHECK(!U(DestroyMenu)(m1));
    EXPECT_ERR(1401);   /* ERROR_INVALID_MENU_HANDLE */
    CHECK(U(DestroyMenu)(m2));
}
T(noparam_0x00_create_menu)       { create_menu_case(0); }
T(noparam_0x01_create_popup_menu) { create_menu_case(1); }

T(noparam_0x00_matches_public_createmenu) {
    NEED_RAW(NtUserCallNoParam);
    HMENU a = U(CreateMenu)(), b = (HMENU)NP(0);
    CHECK(a && b);
    /* same handle class: same type byte, both new objects */
    struct k_user_entry ea, eb;
    if (a && b && k_user_entry((HANDLE)a, &ea) && k_user_entry((HANDLE)b, &eb)) CHECK(ea.type == eb.type);
    if (a) U(DestroyMenu)(a);
    if (b) U(DestroyMenu)(b);
}

T(noparam_0x01_popup_menu_can_hold_items) {
    NEED_RAW(NtUserCallNoParam);
    HMENU m = (HMENU)NP(1);
    if (!m) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuA)(m, MF_STRING, 100, "x"));
    CHECK(U(GetMenuItemCount)(m) == 1);
    CHECK(U(DestroyMenu)(m));
}

/* ---- 0x06 DestroyCaret ------------------------------------------------------------------------ */
/* DestroyCaret: destroys the queue's caret if there is one and returns whether it did.
 * Without a caret window it fails with last error 5. */
T(noparam_0x06_destroy_caret_without_caret) {
    NEED_RAW(NtUserCallNoParam);
    HWND w = k_make_window("r2ncm_caret0");
    if (!w) { t_skip("no window"); return; }
    SetLastError(SENTINEL_ERR);
    ULONG_PTR r = NP(6);
    CHECK((r & 0xff) == 0);
    EXPECT_ERR(ERR_ACCESS_DENIED);
    U(DestroyWindow)(w);
}
T(noparam_0x06_destroy_caret_with_caret) {
    NEED_RAW(NtUserCallNoParam);
    HWND w = k_make_window("r2ncm_caret1");
    if (!w) { t_skip("no window"); return; }
    if (!U(CreateCaret)(w, NULL, 2, 10)) { t_skip("CreateCaret failed"); U(DestroyWindow)(w); return; }
    CHECK((NP(6) & 0xff) == 1);
    SetLastError(SENTINEL_ERR);
    CHECK((NP(6) & 0xff) == 0);          /* caret already gone */
    EXPECT_ERR(ERR_ACCESS_DENIED);
    SetLastError(SENTINEL_ERR);
    CHECK((U(DestroyCaret)() & 0xff) == 0);       /* the public API routes to the same code */
    EXPECT_ERR(ERR_ACCESS_DENIED);
    U(DestroyWindow)(w);
}
T(noparam_0x06_public_destroycaret_roundtrip) {
    NEED_RAW(NtUserCallNoParam);
    HWND w = k_make_window("r2ncm_caret2");
    if (!w) { t_skip("no window"); return; }
    if (!U(CreateCaret)(w, NULL, 3, 9)) { t_skip("CreateCaret failed"); U(DestroyWindow)(w); return; }
    CHECK((U(DestroyCaret)() & 0xff) != 0);
    CHECK((NP(6) & 0xff) == 0);
    U(DestroyWindow)(w);
}

/* ---- 0x0b GetInputDesktop --------------------------------------------------------------------- */
/* _GetInputDesktop looks the input desktop object up among THIS process' handles (ObFindHandleForObject)
 * and returns that existing handle (flag 1 set on it) or NULL: it never creates a handle. */
T(noparam_0x0b_get_input_desktop_returns_existing_handle) {
    NEED_RAW(NtUserCallNoParam);
    HDESK a = (HDESK)NP(0x0b), b = (HDESK)NP(0x0b);
    CHECKF(a == b, "two consecutive calls gave %p and %p (same handle table scan)", a, b);
    if (!a) { t_note("process holds no handle to the input desktop (secure desktop / not interactive)"); return; }
    char name[128] = {0}; DWORD need = 0;
    CHECK(U(GetUserObjectInformationA)(a, UOI_NAME, name, sizeof name, &need));
    t_note("input desktop handle %p name \"%s\"", a, name);
    CHECK(need > 1);
    DWORD flags = 0;
    CHECK(GetHandleInformation((HANDLE)a, &flags));        /* still a live handle of this process */
    HDESK th = U(GetThreadDesktop)(GetCurrentThreadId());
    char n2[128] = {0};
    if (th && U(GetUserObjectInformationA)(th, UOI_NAME, n2, sizeof n2, &need) && k_has_interactive_desktop())
        t_note("thread desktop \"%s\"", n2);
}

/* ---- 0x0c GetMessagePos / 0x0e GetUnpredictedMessagePos ---------------------------------------- */
/* _GetMessagePos reads the per-thread last-message point (pti+0x2fc, packed x|y<<16); with no
 * message retrieved yet (dpi context field pti+0x304 == 0) it returns 0. Same thread -> same DPI
 * awareness context -> no conversion, so after retrieving a message it equals MSG.pt. */
T(noparam_0x0c_message_pos_equals_msg_pt) {
    NEED_RAW(NtUserCallNoParam);
    HWND w = k_make_window("r2ncm_pos");
    if (!w) { t_skip("no window"); return; }
    CHECK(U(PostMessageA)(w, WM_NULL, 0, 0));
    MSG m; memset(&m, 0, sizeof m);
    BOOL got = FALSE;
    for (int i = 0; i < 50 && !got; i++) { got = U(PeekMessageA)(&m, w, WM_NULL, WM_NULL, PM_REMOVE); if (!got) Sleep(5); }
    CHECK(got);
    if (got) {
        DWORD direct = LO32(NP(0x0c));
        DWORD viaapi = U(GetMessagePos)();
        CHECKF(direct == viaapi, "direct 0x%lx public 0x%lx", (unsigned long)direct, (unsigned long)viaapi);
        CHECKF((SHORT)LOWORD(direct) == (SHORT)m.pt.x && (SHORT)HIWORD(direct) == (SHORT)m.pt.y,
               "pos (%d,%d) msg.pt (%ld,%ld)", (SHORT)LOWORD(direct), (SHORT)HIWORD(direct), m.pt.x, m.pt.y);
    }
    U(DestroyWindow)(w);
}
T(noparam_0x0c_message_pos_is_stable_between_calls) {
    NEED_RAW(NtUserCallNoParam);
    k_pump(10);
    DWORD a = LO32(NP(0x0c)), b = LO32(NP(0x0c));
    CHECK(a == b);                       /* a pure read of per-thread state */
    CHECK(U(GetMessagePos)() == a);
}
TCHILD(noparam_fresh_thread_message_pos) {
    (void)argc; (void)argv;
    FARPROC p = t_proc_in("win32u.dll", "NtUserCallNoParam");
    FARPROC g = t_proc_in("user32.dll", "GetMessagePos");     /* connects the process to win32k */
    if (!p || !g) return 0;
    void *(WINAPI *f)(DWORD) = (void *(WINAPI *)(DWORD))p;
    /* No message has been retrieved by this thread, so pti+0x304 (dpi context of the last message) is 0
     * and _GetMessagePos returns 0 on its first branch; user32 start-up may still retrieve one, so this
     * is recorded rather than asserted. The two paths must agree either way. */
    DWORD pos = (DWORD)(ULONG_PTR)f(0x0c);
    DWORD api = ((DWORD (WINAPI *)(void))g)();
    printf("NOTE fresh GUI thread: code 0x0c = 0x%lx, GetMessagePos = 0x%lx\n", (unsigned long)pos, (unsigned long)api);
    if (pos != api) { printf("  FAIL fresh thread direct 0x%lx vs public 0x%lx\n", (unsigned long)pos, (unsigned long)api); return 1; }
    return 0;
}
T(noparam_0x0c_fresh_thread_direct_matches_public) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    struct k_child_result r = k_child_run("r2_ntusercall_codes_matrix_noparam_fresh_thread_message_pos", "", 20000);
    CHECK(r.exit_code == 0);
}

T(noparam_0x0e_unpredicted_pos_matches_public) {
    NEED_RAW(NtUserCallNoParam);
    FARPROC pub = t_proc_in("user32.dll", "GetUnpredictedMessagePos");
    if (!pub) { t_skip("GetUnpredictedMessagePos not exported"); return; }
    k_pump(10);
    DWORD a = LO32(NP(0x0e)), b = ((DWORD (WINAPI *)(void))pub)();
    CHECKF(a == b, "direct 0x%lx public 0x%lx", (unsigned long)a, (unsigned long)b);
}
/* ---- 0x11 IsQueueAttached -------------------------------------------------------------------- */
/* _IsQueueAttached: 1 < queue->cThreads (pq+0x188) */
T(noparam_0x11_queue_attached_default_false) {
    NEED_RAW(NtUserCallNoParam);
    HWND w = k_make_window("r2ncm_q");
    if (!w) { t_skip("no window"); return; }
    CHECK((NP(0x11) & 0xff) == 0);
    FARPROC pub = t_proc_in("user32.dll", "IsQueueAttached");
    if (pub) CHECK((((DWORD (WINAPI *)(void))pub)() & 0xff) == 0);
    U(DestroyWindow)(w);
}
T(noparam_0x11_queue_attached_after_attach_thread_input) {
    NEED_RAW(NtUserCallNoParam);
    HWND w = k_make_window("r2ncm_q2");
    struct mx_bg bg;
    if (!w || !mx_bg_start(&bg, 0)) { t_skip("fixture"); if (w) U(DestroyWindow)(w); mx_bg_stop(&bg); return; }
    DWORD me = GetCurrentThreadId();
    if (!U(AttachThreadInput)(me, bg.tid, TRUE)) { t_skip("AttachThreadInput failed %lu", GetLastError()); }
    else {
        CHECK((NP(0x11) & 0xff) == 1);                          /* cThreads == 2 */
        CHECK(B8(OP(0, 0x4e)) == 1);                               /* IsThreadMessageQueueAttached(0) is the same predicate */
        CHECK(B8(OP(bg.tid, 0x4e)) == 1);                          /* tid shares our queue */
        CHECK(U(AttachThreadInput)(me, bg.tid, FALSE));
        CHECK((NP(0x11) & 0xff) == 0);
        CHECK(B8(OP(bg.tid, 0x4e)) == 0);
    }
    mx_bg_stop(&bg);
    U(DestroyWindow)(w);
}

/* ---- 0x16 ReleaseCapture ------------------------------------------------------------------- */
/* ReleaseCapture: unless queue flag 0x100000 is set -> clearing capture, return 1 */
T(noparam_0x16_release_capture_clears_capture) {
    NEED_RAW(NtUserCallNoParam);
    HWND w = k_make_window("r2ncm_cap");
    if (!w) { t_skip("no window"); return; }
    U(SetCapture)(w);
    CHECK(U(GetCapture)() == w);
    CHECK(NP(0x16) == 1);
    CHECK(U(GetCapture)() == NULL);
    CHECK(NP(0x16) == 1);                                  /* releasing with nothing captured still returns 1 */
    U(SetCapture)(w);
    CHECK(U(ReleaseCapture)() != 0);                       /* public wrapper == code 0x16 */
    CHECK(U(GetCapture)() == NULL);
    U(DestroyWindow)(w);
}
T(noparam_0x16_release_capture_returns_previous_via_setcapture) {
    NEED_RAW(NtUserCallNoParam);
    HWND w = k_make_window("r2ncm_cap2");
    if (!w) { t_skip("no window"); return; }
    CHECK(U(SetCapture)(w) == NULL);
    CHECK(U(SetCapture)(w) == w);
    NP(0x16);
    CHECK(U(SetCapture)(w) == NULL);                       /* capture was really gone */
    NP(0x16);
    U(DestroyWindow)(w);
}

/* ---- codes that must not be invoked here -------------------------------------------------------- */
/* 0x02 AllowForegroundActivation ORs a bit into the session-wide gdwPUDFlags and never clears it;
 * 0x07 DisableProcessWindowsGhosting is irreversible; 0x05/0x09/0x0f/0x1a..0x21/0x25/0x26 are winsrv /
 * logon / shadow / power entry points; 0x24 UpdatePerUserImmEnabling, 0x27/0x28 message-pump hooks,
 * 0x22 ClientThreadSetup state; 0x12/0x13/0x14/0x18/0x19 are client-state helpers used mid-API. They
 * are recorded in the catalogue in ranges.c and are only reached by the inert-range tests above. */

/* ---- 0x2b / 0x29 per-process / per-thread opt-ins: child only --------------------------------- */
TCHILD(noparam_optins) {
    (void)argc; (void)argv;
    mx_init();            /* user32 provides the kernel callback table; without it the first win32k syscall is fatal */
    FARPROC p = t_proc_in("win32u.dll", "NtUserCallNoParam");
    if (!p) return 0;
    void *(WINAPI *f)(DWORD) = (void *(WINAPI *)(DWORD))p;
    int fails = 0;
    ULONG_PTR a = (ULONG_PTR)f(0x2b);
    ULONG_PTR b = (ULONG_PTR)f(0x2b);
    printf("NOTE EnablePerMonitorMenuScaling: first=%llx second=%llx\n", (unsigned long long)a, (unsigned long long)b);
    ULONG_PTR c = (ULONG_PTR)f(0x29);
    ULONG_PTR d = (ULONG_PTR)f(0x29);
    printf("NOTE EnableMouseInPointerForThread: first=%llx second=%llx\n", (unsigned long long)c, (unsigned long long)d);
    /* idempotent opt-ins: a repeated call must give the same answer as the first one */
    if ((a & 0xff) != (b & 0xff)) { printf("  FAIL 0x2b not idempotent\n"); fails++; }
    if ((c & 0xff) != (d & 0xff)) { printf("  FAIL 0x29 not idempotent\n"); fails++; }
    /* and neither disturbs the thread's GUI state */
    void *ti = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &ti);
    if (!ti) { printf("  FAIL no Win32ThreadInfo after opt-in calls\n"); fails++; }
    return fails;
}
T(noparam_0x29_0x2b_optins_in_child) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    struct k_child_result r = k_child_run("r2_ntusercall_codes_matrix_noparam_optins", "", 20000);
    CHECK(r.exit_code == 0);
}

/* ---- 0x08 special-cased before the range check -------------------------------------------------- */
T(noparam_0x08_drain_completions_is_inert) {
    NEED_RAW(NtUserCallNoParam);
    /* EnterSharedCrit + _DrainThreadCoreMessagingCompletions(): no message-loop side effects when the
     * thread has no pending core-messaging completions. Result is a status-like count; recorded only. */
    struct mx_snap a, b; mx_snap_take(&a);
    SetLastError(SENTINEL_ERR);
    ULONG_PTR r = NP(8);
    mx_snap_take(&b);
    t_note("NtUserCallNoParam(8) = 0x%llx", (unsigned long long)r);
    CHECK(mx_snap_equal(&a, &b));
}

/* ---- every safe NoParam code, hammered: state is unchanged and results are stable -------------- */
T(noparam_safe_read_codes_are_idempotent) {
    NEED_RAW(NtUserCallNoParam);
    static const struct { DWORD code; DWORD mask; } safe[] = {
        { 0x0c, 0xffffffffu }, { 0x0e, 0xffffffffu }, { 0x11, 0xff }, { 0x08, 0xffffffffu },
    };
    for (size_t i = 0; i < sizeof safe / sizeof safe[0]; i++) {
        ULONG_PTR first = NP(safe[i].code);
        for (int k = 0; k < 5; k++) {
            ULONG_PTR again = NP(safe[i].code);
            CHECKF((again & safe[i].mask) == (first & safe[i].mask), "code 0x%lx unstable: %llx then %llx",
                   (unsigned long)safe[i].code, (unsigned long long)first, (unsigned long long)again);
        }
    }
}
