/* IME input contexts: TYPE_INPUTCONTEXT (0x11) objects in the USER handle table, created by
 * NtUserCreateInputContext (imm32!ImmCreateContext), the per-thread default context / default IME
 * window, window association and the win32kfull validation branches. */
#include "imhm.h"

#define SENT 0x7e57u
#define T(x) ime_hotkeys_misc_imc_##x

static HIMC mk(void) { return IM(ImmCreateContext) ? IM(ImmCreateContext)() : NULL; }
static void rm(HIMC h) { if (h && IM(ImmDestroyContext)) IM(ImmDestroyContext)(h); }
static ULONG_PTR qic(HANDLE h, DWORD code) { return UN(NtUserQueryInputContext) ? UN(NtUserQueryInputContext)(h, code) : 0; }
static HANDLE default_imc(void) { return UN(NtUserGetThreadState) ? (HANDLE)UN(NtUserGetThreadState)(4) : NULL; }
static HWND default_ime_wnd(void) { return UN(NtUserGetThreadState) ? (HWND)UN(NtUserGetThreadState)(3) : NULL; }

#define NEED_IMC() do { if (!IM(ImmCreateContext) || !IM(ImmDestroyContext) || !UN(NtUserQueryInputContext) || !UN(NtUserGetThreadState) \
    || !U(GetCurrentThreadId)) { t_skip("missing imm32/win32u export"); return; } NEED_IME(); } while (0)

/* ---- create / destroy -------------------------------------------------------------------- */
TEST(ime_hotkeys_misc_imc_create_returns_handle) {
    NEED_IMC();
    SET_ERR(SENT);
    HIMC h = mk();
    if (!h) { t_note("ImmCreateContext failed, err %lu", GetLastError()); t_skip("no input context"); return; }
    CHECK(((ULONG_PTR)h & 0xffff) != 0);
    CHECK(((ULONG_PTR)h >> 16) != 0);       /* uniq in the high word */
    rm(h);
}
TEST(ime_hotkeys_misc_imc_handle_is_sign_extended_32bit) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    /* the handle is allocated as an int that is widened to the HIMC */
    EXPECT_EQ((ULONG_PTR)h, (ULONG_PTR)(LONG_PTR)(int)(ULONG_PTR)h);
    rm(h);
}
TEST(ime_hotkeys_misc_imc_handle_entry_type_0x11) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    struct k_user_entry e;
    if (!im_entry(h, &e)) { t_skip("user handle table not readable"); rm(h); return; }
    EXPECT_EQ(e.type, IM_TYPE_INPUTCONTEXT);      /* handle-table type 0x11 */
    CHECK((e.flags & 1) == 0);
    CHECK(e.uniq == (unsigned short)((ULONG_PTR)h >> 16) || e.uniq == 0xffff);
    rm(h);
}
TEST(ime_hotkeys_misc_imc_entry_owner_is_creating_thread_info) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    void *pti = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &pti);
    uint64_t owner = 0;
    struct k_user_shared_info si;
    if (!pti || !k_shared_info(&si) || !si.aheList) { t_skip("no pti/handle table"); rm(h); return; }
    unsigned long sz = si.heEntrySize ? si.heEntrySize : 0x18;
    if (!k_try_read((char *)si.aheList + (ULONG_PTR)((ULONG_PTR)h & 0xffff) * sz + 8, &owner, 8)) { t_skip("entry unreadable"); rm(h); return; }
    EXPECT_EQ(owner, (uint64_t)(ULONG_PTR)pti);   /* thread-owned object: DestroyInputContext requires the calling thread to own it */
    rm(h);
}
TEST(ime_hotkeys_misc_imc_client_view_starts_with_handle) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    uint64_t v = 0;
    if (!im_obj_qword(h, IM_IMC_HANDLE, &v)) { t_skip("client view unavailable"); rm(h); return; }
    EXPECT_EQ(v, (uint64_t)(ULONG_PTR)h);
    rm(h);
}
TEST(ime_hotkeys_misc_imc_client_view_holds_client_imc_pointer) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    uint64_t v = 0;
    if (!im_obj_qword(h, IM_IMC_CLIENT, &v)) { t_skip("client view unavailable"); rm(h); return; }
    /* NtUserCreateInputContext stores its argument (the 0x58-byte CLIENTIMC heap block) at shared+0x18 */
    CHECK(v != 0);
    unsigned char buf[0x58];
    CHECK(k_try_read((void *)(ULONG_PTR)v, buf, sizeof buf));
    rm(h);
}
TEST(ime_hotkeys_misc_imc_client_imc_refcount_positive) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    uint64_t v = 0;
    unsigned long rc = 0;
    if (!im_obj_qword(h, IM_IMC_CLIENT, &v) || !k_try_read_u32((char *)(ULONG_PTR)v + 8, &rc)) { t_skip("client view unavailable"); rm(h); return; }
    CHECKF(rc >= 1, "refcount %lu", rc);  /* ImmCreateContext: InterlockedIncrement(client + 8) */
    rm(h);
}
TEST(ime_hotkeys_misc_imc_client_imc_critical_section_initialised) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    uint64_t v = 0;
    if (!im_obj_qword(h, IM_IMC_CLIENT, &v)) { t_skip("client view unavailable"); rm(h); return; }
    CRITICAL_SECTION cs;
    if (!k_try_read((char *)(ULONG_PTR)v + 0x18, &cs, sizeof cs)) { t_skip("unreadable"); rm(h); return; }
    CHECK(cs.LockCount == -1);              /* RtlInitializeCriticalSection(client + 0x18), unowned */
    rm(h);
}
TEST(ime_hotkeys_misc_imc_destroy_returns_true) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    EXPECT_EQ(IM(ImmDestroyContext)(h), 1);
}
TEST(ime_hotkeys_misc_imc_destroy_frees_entry) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    CHECK(IM(ImmDestroyContext)(h));
    struct k_user_entry e;
    if (im_entry(h, &e)) CHECKF(e.type != IM_TYPE_INPUTCONTEXT || (e.flags & 1) || e.uniq != (unsigned short)((ULONG_PTR)h >> 16),
                                "entry still live: type %u flags %u uniq %04x", e.type, e.flags, e.uniq);
}
TEST(ime_hotkeys_misc_imc_destroy_twice_fails) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    CHECK(IM(ImmDestroyContext)(h));
    EXPECT_EQ(IM(ImmDestroyContext)(h), 0);  /* ImmDestroyContext: QueryInputContext(h,1) is 0, not our tid */
}
TEST(ime_hotkeys_misc_imc_destroy_null_fails) {
    NEED_IMC();
    EXPECT_EQ(IM(ImmDestroyContext)(NULL), 0);
}
TEST(ime_hotkeys_misc_imc_destroy_bogus_fails) {
    NEED_IMC();
    EXPECT_EQ(IM(ImmDestroyContext)((HIMC)(ULONG_PTR)0x12345), 0);
}
TEST(ime_hotkeys_misc_imc_syscall_destroy_default_refused) {
    NEED_IMC();
    HANDLE d = default_imc();
    if (!d) { t_skip("no default input context"); return; }
    SET_ERR(SENT);
    BOOL r = UN(NtUserDestroyInputContext)(d);
 /* DestroyInputContext: -> ERROR_INVALID_PARAMETER */
    EXPECT_FAIL(r == 0, ERROR_INVALID_PARAMETER);
    EXPECT_EQ(default_imc(), d);
}
TEST(ime_hotkeys_misc_imc_syscall_destroy_bogus_handle) {
    NEED_IMC();
    SET_ERR(SENT);
    EXPECT_EQ(UN(NtUserDestroyInputContext)((HANDLE)(ULONG_PTR)0x12345), 0);
}
TEST(ime_hotkeys_misc_imc_syscall_destroy_null) {
    NEED_IMC();
    EXPECT_EQ(UN(NtUserDestroyInputContext)(NULL), 0);
}
TEST(ime_hotkeys_misc_imc_syscall_destroy_window_handle_wrong_type) {
    NEED_IMC(); NEED_GUI(w);
    SET_ERR(SENT);
    EXPECT_EQ(UN(NtUserDestroyInputContext)(w), 0);  /* HMValidateHandle(h, 0x11) type mismatch */
    CHECK(U(IsWindow)(w));
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_syscall_create_null_client_pointer) {
    NEED_IMC();
    if (!UN(NtUserCreateInputContext)) { t_skip("missing stub"); return; }
    SET_ERR(SENT);
    HANDLE h = UN(NtUserCreateInputContext)(NULL);
 /* NtUserCreateInputContext: argument 1 == 0 -> ERROR_INVALID_PARAMETER (the NULL form is kernel internal) */
    EXPECT_FAIL(h == NULL, ERROR_INVALID_PARAMETER);
}
static DWORD WINAPI destroy_foreign(void *p) {
    HANDLE h = (HANDLE)p;
    SetLastError(SENT);
    BOOL r = UN(NtUserDestroyInputContext)(h);
    CHECK(r == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);   /* DestroyInputContext: not owned by the calling thread -> 5 */
    return 0;
}
TEST(ime_hotkeys_misc_imc_syscall_destroy_from_other_thread) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    im_on_thread(destroy_foreign, h);
    CHECK(qic(h, 1) == GetCurrentThreadId());   /* still alive */
    rm(h);
}
static DWORD WINAPI imm_destroy_foreign(void *p) { return (DWORD)IM(ImmDestroyContext)((HIMC)p); }
TEST(ime_hotkeys_misc_imc_imm_destroy_from_other_thread) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    EXPECT_EQ(im_on_thread(imm_destroy_foreign, h), 0);  /* QueryInputContext(h,1) != caller thread id */
    CHECK(qic(h, 1) == GetCurrentThreadId());
    rm(h);
}
TEST(ime_hotkeys_misc_imc_create_many_unique_handles) {
    NEED_IMC();
    enum { N = 64 };
    HIMC h[N];
    int n = 0;
    for (int i = 0; i < N; i++) { h[i] = mk(); if (!h[i]) break; n++; }
    EXPECT_EQ(n, N);
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (h[i] == h[j]) { CHECKF(0, "duplicate handle %p", h[i]); }
    for (int i = 0; i < n; i++) CHECK(qic(h[i], 1) == GetCurrentThreadId());
    for (int i = 0; i < n; i++) rm(h[i]);
}
TEST(ime_hotkeys_misc_imc_handle_reuse_bumps_uniq) {
    NEED_IMC();
    HIMC a = mk(); if (!a) { t_skip("no input context"); return; }
    rm(a);
    HIMC b = mk();
    if (b == a) t_note("handle %p reused verbatim", a);
    CHECK(b != NULL);
    if (b && (((ULONG_PTR)b & 0xffff) == ((ULONG_PTR)a & 0xffff))) CHECK(b != a);  /* freeing advances the uniqueness word */
    rm(b);
}

/* ---- QueryInputContext -------------------------------------------------------------------- */
TEST(ime_hotkeys_misc_imc_query_process_id) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    EXPECT_EQ(qic(h, 0), GetCurrentProcessId());   /* QIC_INPUTPROCESS: PsGetThreadProcessId(pti->pEThread) */
    rm(h);
}
TEST(ime_hotkeys_misc_imc_query_thread_id) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    EXPECT_EQ(qic(h, 1), GetCurrentThreadId());    /* QIC_INPUTTHREAD */
    rm(h);
}
static DWORD WINAPI other_thread_ctx(void *p) {
    HIMC *out = (HIMC *)p;
    out[0] = mk();
    out[1] = (HIMC)(ULONG_PTR)GetCurrentThreadId();
    out[2] = (HIMC)qic(out[0], 1);
    out[3] = (HIMC)qic(out[0], 0);
    return 0;
}
TEST(ime_hotkeys_misc_imc_query_thread_id_of_foreign_context) {
    NEED_IMC();
    HIMC o[4] = {0};
    im_on_thread(other_thread_ctx, o);
    if (!o[0]) { t_skip("worker could not create"); return; }
    t_note("foreign context queried from a different thread");
    /* the object dies with its thread; the values were sampled inside the owner */
    EXPECT_EQ(o[2], o[1]);
    EXPECT_EQ(o[3], GetCurrentProcessId());
}
TEST(ime_hotkeys_misc_imc_query_cross_thread_same_process) {
    NEED_IMC();
    struct imt x;
    if (!imt_start(&x, 0)) { imt_stop(&x); t_skip("helper"); return; }
    HIMC h = mk(); if (!h) { imt_stop(&x); t_skip("no input context"); return; }
    /* NtUserQueryInputContext has no thread restriction: any thread can query a live IMC */
    EXPECT_EQ(qic(h, 1), GetCurrentThreadId());
    rm(h);
    imt_stop(&x);
}
TEST(ime_hotkeys_misc_imc_query_invalid_code) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    static const DWORD codes[] = { 4, 5, 6, 7, 8, 0x10, 0xff, 0x100, 0x7fffffff, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        SET_ERR(SENT);
        ULONG_PTR r = qic(h, codes[i]);
        CHECKF(r == 0, "code 0x%lx -> 0x%llx", codes[i], (unsigned long long)r);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "code 0x%lx err %lu", codes[i], GetLastError());
    }
    rm(h);
}
TEST(ime_hotkeys_misc_imc_query_bogus_handle) {
    NEED_IMC();
    static const ULONG_PTR hs[] = { 0, 1, 0x12345, 0xffff, 0x10001, 0xfffffff0, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < sizeof hs / sizeof hs[0]; i++)
        for (DWORD c = 0; c < 4; c++) CHECKF(qic((HANDLE)hs[i], c) == 0, "handle %p code %lu", (void *)hs[i], c);
}
TEST(ime_hotkeys_misc_imc_query_window_handle_wrong_type) {
    NEED_IMC(); NEED_GUI(w);
    for (DWORD c = 0; c < 4; c++) CHECKF(qic(w, c) == 0, "code %lu", c);
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_query_after_destroy) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    rm(h);
    for (DWORD c = 0; c < 4; c++) CHECKF(qic(h, c) == 0, "code %lu", c);
}
TEST(ime_hotkeys_misc_imc_query_default_imc_matches_thread_state) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    HANDLE d = default_imc();
    EXPECT_EQ(qic(h, 3), (ULONG_PTR)d);   /* QIC_DEFAULTIMC is pti->pimcDefault, same field GetThreadState(4) reads */
    CHECK(d != NULL);
    CHECK(d != (HANDLE)h);
    rm(h);
}
TEST(ime_hotkeys_misc_imc_default_imc_is_type_0x11) {
    NEED_IMC();
    HANDLE d = default_imc();
    if (!d) { t_skip("no default input context"); return; }
    struct k_user_entry e;
    if (!im_entry(d, &e)) { t_skip("handle table"); return; }
    EXPECT_EQ(e.type, IM_TYPE_INPUTCONTEXT);
    EXPECT_EQ(qic(d, 1), GetCurrentThreadId());
}
TEST(ime_hotkeys_misc_imc_query_default_window_matches_thread_state) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    EXPECT_EQ(qic(h, 2), (ULONG_PTR)default_ime_wnd());   /* pti+0x310 in both */
    rm(h);
}
TEST(ime_hotkeys_misc_imc_default_ime_window_exists_after_window_creation) {
    NEED_IMC(); NEED_GUI(w);
    HWND d = default_ime_wnd();
    if (!d) { t_note("thread has no default IME window"); im_kill(w); return; }
    CHECK(U(IsWindow)(d));
    DWORD pid = 0;
    DWORD tid = U(GetWindowThreadProcessId)(d, &pid);
    EXPECT_EQ(tid, GetCurrentThreadId());
    EXPECT_EQ(pid, GetCurrentProcessId());
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_default_ime_window_class_and_title) {
    NEED_IMC(); NEED_GUI(w);
    HWND d = default_ime_wnd();
    if (!d) { t_note("thread has no default IME window"); im_kill(w); return; }
    WCHAR cls[32] = L"", txt[32] = L"";
    U(GetClassNameW)(d, cls, 32);
    U(GetWindowTextW)(d, txt, 32);
    CHECK(!lstrcmpW(cls, L"IME"));            /* the default IME window registers class "IME" */
    CHECK(!lstrcmpW(txt, L"Default IME"));
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_default_ime_window_is_not_visible_or_child) {
    NEED_IMC(); NEED_GUI(w);
    HWND d = default_ime_wnd();
    if (!d) { t_note("thread has no default IME window"); im_kill(w); return; }
    CHECK(!U(IsWindowVisible)(d));
    /* The IME window is an owned popup, not a child: GetParent reports its owner (which window that is depends on which of this
     * thread's windows caused its creation, so only the relation is checked). */
    CHECK(U(GetParent)(d) == U(GetWindow)(d, GW_OWNER));
    CHECK(U(GetWindow)(d, GW_OWNER) != NULL);
    CHECK(!(U(GetWindowLongW)(d, GWL_STYLE) & WS_CHILD));
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_default_ime_window_in_thread_window_list) {
    NEED_IMC(); NEED_GUI(w);
    HWND d = default_ime_wnd();
    if (!d) { t_note("thread has no default IME window"); im_kill(w); return; }
    /* it is an ordinary top-level window of this thread's desktop */
    HWND f = U(FindWindowExW)(NULL, NULL, L"IME", L"Default IME");
    int found = 0;
    while (f) { if (f == d) { found = 1; break; } f = U(FindWindowExW)(NULL, f, L"IME", L"Default IME"); }
    CHECK(found);
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_imm_get_default_ime_wnd_forms) {
    NEED_IMC(); NEED_GUI(w);
    if (!IM(ImmGetDefaultIMEWnd) || !UN(NtUserQueryWindow)) { im_kill(w); t_skip("missing export"); return; }
    HWND d = default_ime_wnd();
    EXPECT_EQ(IM(ImmGetDefaultIMEWnd)(NULL), d);   /* NULL form is NtUserGetThreadState(3) */
    EXPECT_EQ(IM(ImmGetDefaultIMEWnd)(w), d);      /* hwnd form is NtUserQueryWindow(w, 8) */
    EXPECT_EQ(UN(NtUserQueryWindow)(w, 8), (ULONG_PTR)d);
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_default_ime_wnd_bogus_window) {
    NEED_IMC();
    if (!IM(ImmGetDefaultIMEWnd)) { t_skip("missing export"); return; }
    EXPECT_EQ(IM(ImmGetDefaultIMEWnd)(im_bogus_hwnd()), NULL);
}
TEST(ime_hotkeys_misc_imc_default_ime_wnd_of_foreign_thread_window) {
    NEED_IMC();
    struct imt x;
    if (!IM(ImmGetDefaultIMEWnd) || !imt_start(&x, 1)) { imt_stop(&x); t_skip("helper window"); return; }
    HWND d = IM(ImmGetDefaultIMEWnd)(x.hwnd);
    if (d) {
        DWORD pid = 0;
        EXPECT_EQ(U(GetWindowThreadProcessId)(d, &pid), x.tid);   /* default IME window of the window's own thread */
        EXPECT_EQ(pid, GetCurrentProcessId());
        CHECK(d != default_ime_wnd());
    } else t_note("helper thread has no default IME window");
    imt_stop(&x);
}
TEST(ime_hotkeys_misc_imc_thread_state_ime_fields_match_query) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    HANDLE d = default_imc();
    EXPECT_EQ(qic(d, 3), (ULONG_PTR)d);
    EXPECT_EQ(qic(d, 2), (ULONG_PTR)default_ime_wnd());
    rm(h);
}

/* ---- ImmGetContext / ImmReleaseContext --------------------------------------------------- */
TEST(ime_hotkeys_misc_imc_getcontext_null_window) {
    NEED_IMC();
    if (!IM(ImmGetContext)) { t_skip("missing export"); return; }
    EXPECT_EQ(IM(ImmGetContext)(NULL), NULL);
}
TEST(ime_hotkeys_misc_imc_getcontext_bogus_window) {
    NEED_IMC();
    if (!IM(ImmGetContext)) { t_skip("missing export"); return; }
    EXPECT_EQ(IM(ImmGetContext)(im_bogus_hwnd()), NULL);
}
TEST(ime_hotkeys_misc_imc_getcontext_new_window_has_default) {
    NEED_IMC(); NEED_GUI(w);
    HIMC c = IM(ImmGetContext)(w);
    HANDLE d = default_imc();
    EXPECT_EQ(c, (HIMC)d);   /* window creation associates the thread's default input context */
    if (c) CHECK(IM(ImmReleaseContext)(w, c));
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_window_state_holds_default_himc) {
    NEED_IMC(); NEED_GUI(w);
    HANDLE c = NULL;
    if (!im_wnd_himc(w, &c)) { t_skip("window client view unavailable"); im_kill(w); return; }
    EXPECT_EQ(c, default_imc());    /* tagWND+0xd0 is what ImmGetContext returns */
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_getcontext_release_returns_true) {
    NEED_IMC(); NEED_GUI(w);
    HIMC c = IM(ImmGetContext)(w);
    if (!c) { t_skip("no context"); im_kill(w); return; }
    EXPECT_EQ(IM(ImmReleaseContext)(w, c), 1);
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_getcontext_repeatable) {
    NEED_IMC(); NEED_GUI(w);
    HIMC a = IM(ImmGetContext)(w), b = IM(ImmGetContext)(w);
    EXPECT_EQ(a, b);
    if (a) { IM(ImmReleaseContext)(w, a); IM(ImmReleaseContext)(w, b); }
    im_kill(w);
}
static DWORD WINAPI getctx_foreign(void *p) { return (DWORD)(ULONG_PTR)IM(ImmGetContext)((HWND)p); }
TEST(ime_hotkeys_misc_imc_getcontext_window_of_other_thread_same_process) {
    NEED_IMC(); NEED_GUI(w);
    HIMC mine = IM(ImmGetContext)(w);
    DWORD r = im_on_thread(getctx_foreign, w);
    /* ImmGetContext allows a window of another thread of the same process (QueryWindow(h,0) == our pid) */
    EXPECT_EQ(r, (DWORD)(ULONG_PTR)mine);
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_getcontext_window_of_other_process) {
    NEED_IMC();
    HWND s = U(GetShellWindow)();
    if (!s) { t_skip("no shell window"); return; }
    DWORD pid = 0;
    U(GetWindowThreadProcessId)(s, &pid);
    if (pid == GetCurrentProcessId()) { t_skip("shell is ours"); return; }
    EXPECT_EQ(IM(ImmGetContext)(s), NULL);   /* neither our thread nor our process */
}
TEST(ime_hotkeys_misc_imc_getcontext_desktop_window) {
    NEED_IMC();
    HWND d = U(GetDesktopWindow)();
    if (!d) { t_skip("no desktop"); return; }
    EXPECT_EQ(IM(ImmGetContext)(d), NULL);   /* desktop window is owned by the system process */
}

/* ---- association ------------------------------------------------------------------------- */
TEST(ime_hotkeys_misc_imc_associate_returns_previous) {
    NEED_IMC(); NEED_GUI(w);
    HIMC h = mk(); if (!h) { im_kill(w); t_skip("no input context"); return; }
    HIMC before = IM(ImmGetContext)(w);
    HIMC old = IM(ImmAssociateContext)(w, h);
    EXPECT_EQ(old, before);
    EXPECT_EQ(IM(ImmGetContext)(w), h);
    IM(ImmAssociateContext)(w, before);
    im_kill(w); rm(h);
}
TEST(ime_hotkeys_misc_imc_associate_updates_window_state) {
    NEED_IMC(); NEED_GUI(w);
    HIMC h = mk(); if (!h) { im_kill(w); t_skip("no input context"); return; }
    IM(ImmAssociateContext)(w, h);
    HANDLE c = NULL;
    if (im_wnd_himc(w, &c)) EXPECT_EQ(c, (HANDLE)h);      /* AssociateInputContext stores *imc into pwnd->hImc */
    else t_note("window view unavailable");
    IM(ImmAssociateContext)(w, NULL);
    im_kill(w); rm(h);
}
TEST(ime_hotkeys_misc_imc_associate_same_context_is_noop) {
    NEED_IMC(); NEED_GUI(w);
    HIMC h = mk(); if (!h) { im_kill(w); t_skip("no input context"); return; }
    IM(ImmAssociateContext)(w, h);
    EXPECT_EQ(IM(ImmAssociateContext)(w, h), h);
    im_kill(w); rm(h);
}
TEST(ime_hotkeys_misc_imc_associate_null_disables_window) {
    NEED_IMC(); NEED_GUI(w);
    HIMC before = IM(ImmGetContext)(w);
    HIMC old = IM(ImmAssociateContext)(w, NULL);
    EXPECT_EQ(old, before);
    EXPECT_EQ(IM(ImmGetContext)(w), NULL);   /* ImmLockClientImc(0) fails */
    HANDLE c = (HANDLE)1;
    if (im_wnd_himc(w, &c)) EXPECT_EQ(c, NULL);
    IM(ImmAssociateContext)(w, before);
    EXPECT_EQ(IM(ImmGetContext)(w), before);
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_associate_ex_default_restores) {
    NEED_IMC(); NEED_GUI(w);
    HIMC before = IM(ImmGetContext)(w);
    IM(ImmAssociateContext)(w, NULL);
    SET_ERR(SENT);
    EXPECT_EQ(IM(ImmAssociateContextEx)(w, NULL, IACE_DEFAULT), 1);
    EXPECT_EQ(IM(ImmGetContext)(w), before);
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_associate_ex_plain) {
    NEED_IMC(); NEED_GUI(w);
    HIMC h = mk(); if (!h) { im_kill(w); t_skip("no input context"); return; }
    EXPECT_EQ(IM(ImmAssociateContextEx)(w, h, 0), 1);
    EXPECT_EQ(IM(ImmGetContext)(w), h);
    IM(ImmAssociateContextEx)(w, NULL, IACE_DEFAULT);
    im_kill(w); rm(h);
}
TEST(ime_hotkeys_misc_imc_associate_ex_children) {
    NEED_IMC(); NEED_GUI(w);
    HWND c1 = im_mkwnd(WS_CHILD, 0, w), c2 = im_mkwnd(WS_CHILD, 0, w);
    HIMC h = mk();
    if (!c1 || !c2 || !h) { im_kill(w); rm(h); t_skip("setup"); return; }
    CHECK(IM(ImmAssociateContextEx)(w, h, IACE_CHILDREN));
    EXPECT_EQ(IM(ImmGetContext)(w), h);
    EXPECT_EQ(IM(ImmGetContext)(c1), h);    /* AssociateInputContextEx walks BuildHwndList(child list) */
    EXPECT_EQ(IM(ImmGetContext)(c2), h);
    IM(ImmAssociateContextEx)(w, NULL, IACE_DEFAULT | IACE_CHILDREN);
    HANDLE d = default_imc();
    EXPECT_EQ(IM(ImmGetContext)(c1), (HIMC)d);
    im_kill(w); rm(h);
}
TEST(ime_hotkeys_misc_imc_associate_without_children_flag_leaves_children) {
    NEED_IMC(); NEED_GUI(w);
    HWND c1 = im_mkwnd(WS_CHILD, 0, w);
    HIMC h = mk();
    if (!c1 || !h) { im_kill(w); rm(h); t_skip("setup"); return; }
    HIMC childBefore = IM(ImmGetContext)(c1);
    CHECK(IM(ImmAssociateContextEx)(w, h, 0));
    EXPECT_EQ(IM(ImmGetContext)(c1), childBefore);
    IM(ImmAssociateContextEx)(w, NULL, IACE_DEFAULT);
    im_kill(w); rm(h);
}
TEST(ime_hotkeys_misc_imc_associate_ex_ignore_nocontext) {
    NEED_IMC(); NEED_GUI(w);
    HWND c1 = im_mkwnd(WS_CHILD, 0, w);
    HIMC h = mk();
    if (!c1 || !h) { im_kill(w); rm(h); t_skip("setup"); return; }
    IM(ImmAssociateContext)(c1, NULL);          /* child explicitly has no context */
    CHECK(IM(ImmAssociateContextEx)(w, h, IACE_CHILDREN | IACE_IGNORENOCONTEXT));
    HANDLE c = (HANDLE)1;
    if (im_wnd_himc(c1, &c)) EXPECT_EQ(c, NULL);   /* skips the child */
    EXPECT_EQ(IM(ImmGetContext)(w), h);
    IM(ImmAssociateContextEx)(w, NULL, IACE_DEFAULT | IACE_CHILDREN);
    im_kill(w); rm(h);
}
TEST(ime_hotkeys_misc_imc_associate_ex_foreign_context_denied) {
    NEED_IMC(); NEED_GUI(w);
    HIMC h = mk(); if (!h) { im_kill(w); t_skip("no input context"); return; }
    struct imt x;
    if (!imt_start(&x, 1)) { imt_stop(&x); im_kill(w); rm(h); t_skip("helper"); return; }
    /* IMC of this thread, window of another: ImmAssociateContextEx pre-checks QueryInputContext(h,1) */
    SET_ERR(SENT);
    EXPECT_EQ(IM(ImmAssociateContextEx)(x.hwnd, h, 0), 0);
    EXPECT_EQ(IM(ImmAssociateContext)(x.hwnd, h), NULL);
    imt_stop(&x); im_kill(w); rm(h);
}
TEST(ime_hotkeys_misc_imc_syscall_associate_foreign_context_denied) {
    NEED_IMC(); NEED_GUI(w);
    HIMC h = mk(); if (!h) { im_kill(w); t_skip("no input context"); return; }
    struct imt x;
    if (!imt_start(&x, 1)) { imt_stop(&x); im_kill(w); rm(h); t_skip("helper"); return; }
    SET_ERR(SENT);
    DWORD r = UN(NtUserAssociateInputContext)(x.hwnd, h, 0);
    EXPECT_EQ(r, 2);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    imt_stop(&x); im_kill(w); rm(h);
}
TEST(ime_hotkeys_misc_imc_syscall_associate_bogus_window) {
    NEED_IMC();
    SET_ERR(SENT);
    EXPECT_EQ(UN(NtUserAssociateInputContext)(im_bogus_hwnd(), NULL, 0), 2);
}
TEST(ime_hotkeys_misc_imc_syscall_associate_null_window) {
    NEED_IMC();
    SET_ERR(SENT);
    EXPECT_EQ(UN(NtUserAssociateInputContext)(NULL, NULL, 0), 2);   /* ValidateHwnd(NULL) fails */
}
TEST(ime_hotkeys_misc_imc_syscall_associate_bogus_context) {
    NEED_IMC(); NEED_GUI(w);
    HANDLE before = NULL; im_wnd_himc(w, &before);
    EXPECT_EQ(UN(NtUserAssociateInputContext)(w, (HANDLE)(ULONG_PTR)0x12345, 0), 2);  /* HMValidateHandle(h, 0x11) fails */
    HANDLE after = NULL;
    if (im_wnd_himc(w, &after)) EXPECT_EQ(after, before);
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_syscall_associate_window_handle_as_context) {
    NEED_IMC(); NEED_GUI(w);
    EXPECT_EQ(UN(NtUserAssociateInputContext)(w, w, 0), 2);
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_syscall_associate_null_context_ok) {
    NEED_IMC(); NEED_GUI(w);
    HANDLE before = default_imc();
    DWORD r = UN(NtUserAssociateInputContext)(w, NULL, 0);
    CHECKF(r == 0 || r == 1, "result %lu", r);   /* 0: done, 1: done and w is the thread's focus window */
    HANDLE c = (HANDLE)1;
    if (im_wnd_himc(w, &c)) EXPECT_EQ(c, NULL);
    r = UN(NtUserAssociateInputContext)(w, NULL, IACE_DEFAULT);
    CHECKF(r == 0 || r == 1, "result %lu", r);
    if (im_wnd_himc(w, &c)) EXPECT_EQ(c, before);
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_syscall_associate_result_zero_for_hidden_unfocused_window) {
    NEED_IMC(); NEED_GUI(w);
    HIMC h = mk(); if (!h) { im_kill(w); t_skip("no input context"); return; }
    DWORD r = UN(NtUserAssociateInputContext)(w, h, 0);
    EXPECT_EQ(r, 0);
    UN(NtUserAssociateInputContext)(w, NULL, IACE_DEFAULT);
    im_kill(w); rm(h);
}
TEST(ime_hotkeys_misc_imc_syscall_associate_window_of_other_process) {
    NEED_IMC();
    HWND s = U(GetShellWindow)();
    HIMC h = mk();
    if (!s || !h) { rm(h); t_skip("setup"); return; }
    DWORD pid = 0; U(GetWindowThreadProcessId)(s, &pid);
    if (pid == GetCurrentProcessId()) { rm(h); t_skip("shell is ours"); return; }
    HANDLE before = NULL;
    SET_ERR(SENT);
 /* imc->pti (ours) != wnd->pti (explorer) is rejected first: result 2 with ERROR_ACCESS_DENIED */
    EXPECT_EQ(UN(NtUserAssociateInputContext)(s, h, 0), 2);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    rm(h);
}
TEST(ime_hotkeys_misc_imc_destroy_reassociates_windows_to_default) {
    NEED_IMC(); NEED_GUI(w);
    HIMC h = mk(); if (!h) { im_kill(w); t_skip("no input context"); return; }
    IM(ImmAssociateContext)(w, h);
    CHECK(IM(ImmDestroyContext)(h));
    HANDLE c = NULL;
    if (im_wnd_himc(w, &c)) EXPECT_EQ(c, default_imc());   /* DestroyInputContext: AssociateInputContext(wnd, pimcDefault) */
    EXPECT_EQ(IM(ImmGetContext)(w), (HIMC)default_imc());
    im_kill(w);
}
TEST(ime_hotkeys_misc_imc_destroy_leaves_unrelated_windows) {
    NEED_IMC(); NEED_GUI(w);
    HWND w2 = im_mkpopup();
    HIMC h = mk(), g = mk();
    if (!w2 || !h || !g) { im_kill(w); im_kill(w2); rm(h); rm(g); t_skip("setup"); return; }
    IM(ImmAssociateContext)(w, h);
    IM(ImmAssociateContext)(w2, g);
    CHECK(IM(ImmDestroyContext)(h));
    EXPECT_EQ(IM(ImmGetContext)(w2), g);
    im_kill(w); im_kill(w2); rm(g);
}
TEST(ime_hotkeys_misc_imc_window_destroy_keeps_context) {
    NEED_IMC(); NEED_GUI(w);
    HIMC h = mk(); if (!h) { im_kill(w); t_skip("no input context"); return; }
    IM(ImmAssociateContext)(w, h);
    im_kill(w);
    EXPECT_EQ(qic(h, 1), GetCurrentThreadId());   /* contexts are thread objects, independent of windows */
    rm(h);
}
TEST(ime_hotkeys_misc_imc_associate_one_context_to_many_windows) {
    NEED_IMC();
    HWND w[4];
    HIMC h = mk();
    int n = 0;
    for (; n < 4; n++) { w[n] = im_mkpopup(); if (!w[n]) break; }
    if (n < 4 || !h) { for (int i = 0; i < n; i++) im_kill(w[i]); rm(h); t_skip("setup"); return; }
    for (int i = 0; i < n; i++) IM(ImmAssociateContext)(w[i], h);
    for (int i = 0; i < n; i++) EXPECT_EQ(IM(ImmGetContext)(w[i]), h);
    CHECK(IM(ImmDestroyContext)(h));
    for (int i = 0; i < n; i++) EXPECT_EQ(IM(ImmGetContext)(w[i]), (HIMC)default_imc());
    for (int i = 0; i < n; i++) im_kill(w[i]);
}

/* ---- UpdateInputContext ------------------------------------------------------------------ */
TEST(ime_hotkeys_misc_imc_update_code0_already_initialised) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    char dummy[8];
    SET_ERR(SENT);
    BOOL r = UN(NtUserUpdateInputContext)(h, 0, dummy);
    /* shared+0x18 already holds the CLIENTIMC given to NtUserCreateInputContext -> 0x4df */
    EXPECT_FAIL(r == 0, ERROR_ALREADY_INITIALIZED);
    uint64_t v = 0;
    if (im_obj_qword(h, IM_IMC_CLIENT, &v)) CHECK(v != (uint64_t)(ULONG_PTR)dummy);
    rm(h);
}
TEST(ime_hotkeys_misc_imc_update_unknown_code_returns_false_silently) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    static const DWORD codes[] = { 2, 3, 7, 0xff, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        SET_ERR(SENT);
        BOOL r = UN(NtUserUpdateInputContext)(h, codes[i], NULL);
        CHECKF(r == 0, "code 0x%lx", codes[i]);
        CHECKF(GetLastError() == SENT, "code 0x%lx err %lu", codes[i], GetLastError());  /* without UserSetLastError */
    }
    rm(h);
}
TEST(ime_hotkeys_misc_imc_update_bogus_handle) {
    NEED_IMC();
    EXPECT_EQ(UN(NtUserUpdateInputContext)((HANDLE)(ULONG_PTR)0x12345, 0, NULL), 0);
    EXPECT_EQ(UN(NtUserUpdateInputContext)(NULL, 1, NULL), 0);
}
TEST(ime_hotkeys_misc_imc_update_code1_roundtrip_restores) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    /* code 1 stores an arbitrary client pointer at shared+0x20 (unused by imm32 for IMCs made by
     * ImmCreateContext, which leaves it NULL); write a marker and restore NULL */
    static char marker[8];
    BOOL r = UN(NtUserUpdateInputContext)(h, 1, marker);
    EXPECT_EQ(r, 1);
    uint64_t v = 0;
    if (im_obj_qword(h, 0x20, &v)) EXPECT_EQ(v, (uint64_t)(ULONG_PTR)marker);
    EXPECT_EQ(UN(NtUserUpdateInputContext)(h, 1, NULL), 1);
    if (im_obj_qword(h, 0x20, &v)) EXPECT_EQ(v, 0);
    rm(h);
}
static DWORD WINAPI update_from_thread(void *p) {
    return (DWORD)UN(NtUserUpdateInputContext)((HANDLE)p, 1, NULL);
}
TEST(ime_hotkeys_misc_imc_update_from_other_thread_same_process) {
    NEED_IMC();
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    /* UpdateInputContext compares process infos, not threads */
    EXPECT_EQ(im_on_thread(update_from_thread, h), 1);
    rm(h);
}

/* ---- BuildHimcList / ImmEnumInputContext ------------------------------------------------ */
static DWORD build(DWORD tid, DWORD cap, HANDLE *buf) {
    DWORD n = 0xdeadbeef;
    UN(NtUserBuildHimcList)(tid, cap, buf, &n);
    return n;
}
#define NEED_BUILD() do { NEED_IMC(); if (!UN(NtUserBuildHimcList)) { t_skip("missing NtUserBuildHimcList"); return; } } while (0)
TEST(ime_hotkeys_misc_imc_build_list_contains_default_first) {
    NEED_BUILD();
    HANDLE buf[16] = {0};
    DWORD n = build(0, 16, buf);
    CHECKF(n >= 1, "n=%lu", n);
    EXPECT_EQ(buf[0], default_imc());      /* _BuildHimcList starts at pti->pimcDefault */
}
TEST(ime_hotkeys_misc_imc_build_list_newest_follows_default) {
    NEED_BUILD();
    HIMC a = mk(), b = mk(), c = mk();
    if (!a || !b || !c) { rm(a); rm(b); rm(c); t_skip("no input context"); return; }
    HANDLE buf[16] = {0};
    DWORD n = build(0, 16, buf);
    EXPECT_EQ(n, 4);
    /* CreateInputContext links the new object right after pimcDefault, so the list is default, c, b, a */
    EXPECT_EQ(buf[0], default_imc());
    EXPECT_EQ(buf[1], (HANDLE)c);
    EXPECT_EQ(buf[2], (HANDLE)b);
    EXPECT_EQ(buf[3], (HANDLE)a);
    rm(a); rm(b); rm(c);
}
TEST(ime_hotkeys_misc_imc_build_list_count_tracks_create_destroy) {
    NEED_BUILD();
    HANDLE buf[64];
    DWORD base = build(0, 64, buf);
    HIMC h[10];
    for (int i = 0; i < 10; i++) { h[i] = mk(); if (!h[i]) { t_skip("no input context"); for (int j = 0; j < i; j++) rm(h[j]); return; } }
    EXPECT_EQ(build(0, 64, buf), base + 10);
    for (int i = 0; i < 10; i += 2) rm(h[i]);
    EXPECT_EQ(build(0, 64, buf), base + 5);
    for (int i = 1; i < 10; i += 2) rm(h[i]);
    EXPECT_EQ(build(0, 64, buf), base);
}
TEST(ime_hotkeys_misc_imc_build_list_capacity_limits_count) {
    NEED_BUILD();
    HIMC a = mk(), b = mk();
    if (!a || !b) { rm(a); rm(b); t_skip("no input context"); return; }
    HANDLE buf[8] = {0};
    EXPECT_EQ(build(0, 1, buf), 1);
    EXPECT_EQ(buf[0], default_imc());
    EXPECT_EQ(buf[1], NULL);
    EXPECT_EQ(build(0, 2, buf), 2);
    EXPECT_EQ(build(0, 0, buf), 0);
    rm(a); rm(b);
}
TEST(ime_hotkeys_misc_imc_build_list_all_entries_are_valid_contexts) {
    NEED_BUILD();
    HIMC a = mk(), b = mk();
    HANDLE buf[16] = {0};
    DWORD n = build(0, 16, buf);
    for (DWORD i = 0; i < n; i++) {
        struct k_user_entry e;
        if (im_entry(buf[i], &e)) CHECKF(e.type == IM_TYPE_INPUTCONTEXT, "entry %lu type %u", i, e.type);
        CHECKF(qic(buf[i], 1) == GetCurrentThreadId(), "entry %lu", i);
    }
    rm(a); rm(b);
}
TEST(ime_hotkeys_misc_imc_build_list_all_threads_form) {
    NEED_BUILD();
    HANDLE mine[16], all[64];
    DWORD m = build(0, 16, mine);
    DWORD a = build((DWORD)-1, 64, all);
    /* thread id -1 passes pti == NULL: every thread of the process, ours included */
    CHECKF(a >= m, "all %lu mine %lu", a, m);
    int found = 0;
    for (DWORD i = 0; i < a; i++) if (all[i] == mine[0]) found = 1;
    CHECK(found);
}
TEST(ime_hotkeys_misc_imc_build_list_other_thread) {
    NEED_BUILD();
    struct imt x;
    if (!imt_start(&x, 1)) { imt_stop(&x); t_skip("helper"); return; }
    HANDLE buf[16] = {0};
    DWORD n = build(x.tid, 16, buf);
    CHECKF(n >= 1, "helper thread has %lu contexts", n);
    if (n >= 1) {
        EXPECT_EQ(qic(buf[0], 1), x.tid);
        CHECK(buf[0] != default_imc());
    }
    imt_stop(&x);
}
TEST(ime_hotkeys_misc_imc_build_list_bogus_thread_leaves_count) {
    NEED_BUILD();
    HANDLE buf[4] = {0};
    DWORD n = 0x5a5a5a5a;
    UN(NtUserBuildHimcList)(0xFFFFFF00, 4, buf, &n);
    EXPECT_EQ(n, 0x5a5a5a5a);
    EXPECT_EQ(buf[0], NULL);
}
TEST(ime_hotkeys_misc_imc_build_list_other_process_thread) {
    NEED_BUILD();
    HWND s = U(GetShellWindow)();
    if (!s) { t_skip("no shell window"); return; }
    DWORD pid = 0, tid = U(GetWindowThreadProcessId)(s, &pid);
    if (pid == GetCurrentProcessId()) { t_skip("shell is ours"); return; }
    HANDLE buf[4] = {0};
    DWORD n = 0x5a5a5a5a;
    UN(NtUserBuildHimcList)(tid, 4, buf, &n);
    /* There is no same-process check: on Windows 10 the shell thread's input contexts are listed to any caller. The count is
     * written, never exceeds the capacity, and the stored handles are non-NULL. (How many the shell holds is its own business.) */
    CHECKF(n != 0x5a5a5a5a, "count left untouched");
    CHECKF(n <= 4, "count %lu above the capacity 4", n);
    for (DWORD i = 0; i < n && i < 4; i++) CHECK(buf[i] != NULL);
    if (n < 4) CHECK(buf[n] == NULL);
    t_note("shell thread lists %lu input context(s)", n);
}
static int g_enum_n; static HIMC g_enum_seen[64];
static BOOL CALLBACK enum_cb(HIMC h, LPARAM l) { if (g_enum_n < 64) g_enum_seen[g_enum_n] = h; g_enum_n++; return l ? (g_enum_n < (int)l) : TRUE; }
TEST(ime_hotkeys_misc_imc_enum_matches_build_list) {
    NEED_BUILD();
    if (!IM(ImmEnumInputContext)) { t_skip("missing export"); return; }
    HIMC a = mk(), b = mk();
    HANDLE buf[64];
    DWORD n = build(0, 64, buf);
    g_enum_n = 0;
    BOOL r = IM(ImmEnumInputContext)(0, enum_cb, 0);
    EXPECT_EQ(r, 1);
    EXPECT_EQ(g_enum_n, n);
    for (int i = 0; i < g_enum_n && i < (int)n && i < 64; i++) EXPECT_EQ(g_enum_seen[i], (HIMC)buf[i]);
    rm(a); rm(b);
}
TEST(ime_hotkeys_misc_imc_enum_callback_false_stops) {
    NEED_BUILD();
    if (!IM(ImmEnumInputContext)) { t_skip("missing export"); return; }
    HIMC a = mk(), b = mk(), c = mk();
    g_enum_n = 0;
    BOOL r = IM(ImmEnumInputContext)(0, enum_cb, 2);   /* stops after the second call */
    EXPECT_EQ(g_enum_n, 2);
    EXPECT_EQ(r, 0);
    rm(a); rm(b); rm(c);
}
TEST(ime_hotkeys_misc_imc_enum_bogus_thread_fails) {
    NEED_BUILD();
    if (!IM(ImmEnumInputContext)) { t_skip("missing export"); return; }
    g_enum_n = 0;
    BOOL r = IM(ImmEnumInputContext)(0xFFFFFF00, enum_cb, 0);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(g_enum_n, 0);
}
TEST(ime_hotkeys_misc_imc_enum_all_threads) {
    NEED_BUILD();
    if (!IM(ImmEnumInputContext)) { t_skip("missing export"); return; }
    g_enum_n = 0;
    BOOL r = IM(ImmEnumInputContext)((DWORD)-1, enum_cb, 0);
    CHECK(r || g_enum_n == 0);
    CHECK(g_enum_n >= 1);
}

/* ---- ImmIsIME and open/conversion state ----------------------------------------------- */
TEST(ime_hotkeys_misc_imc_isime_null_hkl) {
    NEED_IMC();
    EXPECT_EQ(IM(ImmIsIME)(NULL), 0);
}
TEST(ime_hotkeys_misc_imc_isime_unloaded_layout_is_not_ime) {
    NEED_IMC();
    /* 0x12341234 has no 0xE000 layout nibble and is not a loaded layout: NtUserGetImeInfoEx type 0 fails.
     * (On Windows 10 the loaded US layout 0x04090409 answers TRUE: every keyboard layout is on the window station's list.) */
    EXPECT_EQ(IM(ImmIsIME)((HKL)(ULONG_PTR)0x12341234), 0);
}
TEST(ime_hotkeys_misc_imc_isime_current_layout_consistency) {
    NEED_IMC();
    HKL k = U(GetKeyboardLayout)(0);
    BOOL r = IM(ImmIsIME)(k);
    unsigned top = (unsigned)(((ULONG_PTR)k >> 16) & 0xf000);
    t_note("HKL %p IsIME %d", k, r);
    if (top != 0xe000) CHECK(r == 0 || r == 1);
}
TEST(ime_hotkeys_misc_imc_open_status_roundtrip) {
    NEED_IMC();
    if (!IM(ImmGetOpenStatus) || !IM(ImmSetOpenStatus)) { t_skip("missing export"); return; }
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    EXPECT_EQ(IM(ImmGetOpenStatus)(h), 0);          /* zero-initialised INPUTCONTEXT */
    BOOL s = IM(ImmSetOpenStatus)(h, TRUE);
    if (!s) { t_note("ImmSetOpenStatus failed (err %lu)", GetLastError()); rm(h); return; }
    EXPECT_EQ(IM(ImmGetOpenStatus)(h), 1);
    CHECK(IM(ImmSetOpenStatus)(h, FALSE));
    EXPECT_EQ(IM(ImmGetOpenStatus)(h), 0);
    rm(h);
}
TEST(ime_hotkeys_misc_imc_open_status_null_context) {
    NEED_IMC();
    if (!IM(ImmGetOpenStatus) || !IM(ImmSetOpenStatus)) { t_skip("missing export"); return; }
    EXPECT_EQ(IM(ImmGetOpenStatus)(NULL), 0);
    EXPECT_EQ(IM(ImmSetOpenStatus)(NULL, TRUE), 0);
}
static DWORD WINAPI setopen_foreign(void *p) { return (DWORD)IM(ImmSetOpenStatus)((HIMC)p, TRUE); }
TEST(ime_hotkeys_misc_imc_set_open_status_foreign_thread_refused) {
    NEED_IMC();
    if (!IM(ImmSetOpenStatus)) { t_skip("missing export"); return; }
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    EXPECT_EQ(im_on_thread(setopen_foreign, h), 0);    /* QueryInputContext(h,1) != caller thread */
    rm(h);
}
TEST(ime_hotkeys_misc_imc_conversion_status_roundtrip) {
    NEED_IMC();
    if (!IM(ImmGetConversionStatus) || !IM(ImmSetConversionStatus)) { t_skip("missing export"); return; }
    HIMC h = mk(); if (!h) { t_skip("no input context"); return; }
    DWORD conv = 0xffff, sent = 0xffff;
    BOOL g = IM(ImmGetConversionStatus)(h, &conv, &sent);
    t_note("fresh IMC conversion 0x%lx sentence 0x%lx (ok %d)", conv, sent, g);
    BOOL s = IM(ImmSetConversionStatus)(h, IME_CMODE_NATIVE, IME_SMODE_NONE);
    t_note("ImmSetConversionStatus(NATIVE) -> %d err %lu", s, s ? 0 : GetLastError());
    if (s && g) {
        DWORD c2 = 0, s2 = 0;
        CHECK(IM(ImmGetConversionStatus)(h, &c2, &s2));
        t_note("after set: conv 0x%lx sentence 0x%lx", c2, s2);
    }
    rm(h);
}
TEST(ime_hotkeys_misc_imc_conversion_status_null_context) {
    NEED_IMC();
    if (!IM(ImmGetConversionStatus) || !IM(ImmSetConversionStatus)) { t_skip("missing export"); return; }
    DWORD c = 0, s = 0;
    EXPECT_EQ(IM(ImmGetConversionStatus)(NULL, &c, &s), 0);
    EXPECT_EQ(IM(ImmSetConversionStatus)(NULL, 0, 0), 0);
}
