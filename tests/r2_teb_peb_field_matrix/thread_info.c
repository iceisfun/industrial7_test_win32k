/* Win32ThreadInfo and the _CLIENTINFO block of fresh GUI threads and of the main thread, compared with each
 * other and with what the kernel reports for the same thread. Layout comments: see rtp.h. */
#include "rtp.h"

/* ---- ops run inside a worker ------------------------------------------------------------------------ */
static void op_kbd(struct wk *w) {
    NtUserGetKeyboardLayout_pfn f = (NtUserGetKeyboardLayout_pfn)t_proc_in("win32u.dll", "NtUserGetKeyboardLayout");
    HKL (WINAPI *g)(DWORD) = (HKL (WINAPI *)(DWORD))t_proc("GetKeyboardLayout");
    w->x[0] = f ? (unsigned long long)(ULONG_PTR)f(0) : 0;
    w->x[1] = g ? (unsigned long long)(ULONG_PTR)g(0) : 0;
}
static void op_more_calls(struct wk *w) {
    HWND (WINAPI *gf)(void) = (HWND (WINAPI *)(void))t_proc("GetFocus");
    HWND (WINAPI *ga)(void) = (HWND (WINAPI *)(void))t_proc("GetActiveWindow");
    DWORD (WINAPI *gp)(void) = (DWORD (WINAPI *)(void))t_proc("GetMessagePos");
    HDC (WINAPI *gdc)(HWND) = (HDC (WINAPI *)(HWND))t_proc("GetDC");
    int (WINAPI *rdc)(HWND, HDC) = (int (WINAPI *)(HWND, HDC))t_proc("ReleaseDC");
    HDC (WINAPI *cdc)(HDC) = (HDC (WINAPI *)(HDC))t_proc("CreateCompatibleDC");
    BOOL (WINAPI *ddc)(HDC) = (BOOL (WINAPI *)(HDC))t_proc("DeleteDC");
    struct rsnap s0; rs_take(&s0);
    if (gf) gf();
    if (ga) ga();
    if (gp) gp();
    if (gdc && rdc) { HDC d = gdc(NULL); if (d) rdc(NULL, d); }
    if (cdc && ddc) { HDC d = cdc(NULL); if (d) ddc(d); }
    rt_ts(0xE);
    struct rsnap s1; rs_take(&s1);
    w->x[0] = s0.ti; w->x[1] = s1.ti;
    w->x[2] = rs_ci64(&s0, CIO_pDeskInfo); w->x[3] = rs_ci64(&s1, CIO_pDeskInfo);
    w->x[4] = rs_ci64(&s0, CIO_ulClientDelta); w->x[5] = rs_ci64(&s1, CIO_ulClientDelta);
    w->x[6] = rs_ci32(&s0, CIO_dwExpWinVer); w->x[7] = rs_ci32(&s1, CIO_dwExpWinVer);
}

/* ---- Win32ThreadInfo ------------------------------------------------------------------------------- */

TEST(r2_teb_peb_field_matrix_ti_main_nonzero_after_gui) {
    struct rsnap m; rt_main_gui(&m);
    CHECK(m.ti != 0);                                   /* converted by the first win32k service */
}
TEST(r2_teb_peb_field_matrix_ti_main_stable_across_calls) {
    struct rsnap a, b; rt_main_gui(&a);
    t_proc("GetFocus"); rt_ts(0xE);
    rs_take(&b);
    CHECK(a.ti != 0);
    EXPECT_EQ(a.ti, b.ti);                              /* the thread info pointer never changes for a live thread */
}
TEST(r2_teb_peb_field_matrix_ti_main_stable_across_gdi_and_user) {
    struct rsnap a, b; rt_main_gui(&a);
    HDC (WINAPI *cdc)(HDC) = (HDC (WINAPI *)(HDC))t_proc("CreateCompatibleDC");
    BOOL (WINAPI *ddc)(HDC) = (BOOL (WINAPI *)(HDC))t_proc("DeleteDC");
    REQUIRE(cdc); REQUIRE(ddc);
    HDC d = cdc(NULL); if (d) ddc(d);
    rs_take(&b);
    EXPECT_EQ(a.ti, b.ti);
}
TEST(r2_teb_peb_field_matrix_ti_main_is_thread_id_per_xxxcreatethreadinfo) {
    /* At thread info creation: *(TEB + 0x78) = PsGetThreadId(thread) */
    struct rsnap m; rt_main_gui(&m);
    CHECK(m.ti != 0);
    EXPECT_EQ(m.ti, GetCurrentThreadId());              /* PsGetThreadId stored unconditionally when the TEB is known */
    EXPECT_EQ(m.ti, m.tid);
}
TEST(r2_teb_peb_field_matrix_ti_worker_nonzero_after_gui) {
    WK_GUI(w);
    CHECK(w.after.ti != 0);
    EXPECT_EQ(w.after.ti, w.tid);                       /* thread info creation: TEB+0x78 = PsGetThreadId */
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ti_worker_before_gui_note) {
    WK_GUI(w);
    if (w.before.ti) t_note("new thread of a GUI process already had Win32ThreadInfo=0x%llx before its first win32k call", w.before.ti);
    CHECK(w.after.ti != 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ti_plain_worker_stays_zero_without_win32k_call) {
    /* a thread that makes no win32k service call is never converted, even though the process is a GUI process */
    rt_gui();
    WK_PLAIN(w);
    if (w.after.ti) t_note("unconverted thread has Win32ThreadInfo=0x%llx (user32 DLL_THREAD_ATTACH makes a win32k call?)", w.after.ti);
    CHECK(w.after.ok);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ti_worker_differs_from_main) {
    struct rsnap m; rt_main_gui(&m);
    WK_GUI(w);
    CHECK(w.after.ti != 0 && m.ti != 0);
    CHECK(w.after.ti != m.ti);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ti_two_live_workers_differ) {
    struct wk a, b;
    if (!wk_start(&a, 1, NULL, NULL) || !wk_start(&b, 1, NULL, NULL)) { wk_finish(&a); wk_finish(&b); t_skip("workers did not start"); return; }
    CHECK(a.after.ti != 0 && b.after.ti != 0);
    CHECK(a.after.ti != b.after.ti);
    CHECK(a.tid != b.tid);
    wk_finish(&a); wk_finish(&b);
}
TEST(r2_teb_peb_field_matrix_ti_worker_stable_across_calls) {
    struct wk w;
    if (!wk_start(&w, 1, op_more_calls, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    CHECK(w.x[0] != 0);
    EXPECT_EQ(w.x[0], w.x[1]);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ti_remote_read_matches_self_read) {
    WK_GUI(w);
    ULONG_PTR teb = rt_thread_teb(w.th);
    CHECK(teb != 0);
    EXPECT_EQ(teb, w.teb);                              /* NtQueryInformationThread agrees with the thread's own k_teb() */
    struct rsnap r; rs_take_teb(&r, teb);
    CHECK(r.ok);
    EXPECT_EQ(r.ti, w.after.ti);
    EXPECT_EQ(r.tid, w.tid);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ti_remote_read_of_parked_thread_is_stable) {
    WK_GUI(w);
    struct rsnap r1, r2;
    rs_take_teb(&r1, w.teb); Sleep(20); rs_take_teb(&r2, w.teb);
    EXPECT_EQ(r1.ti, r2.ti);
    EXPECT_EQ(rs_ci64(&r1, CIO_pDeskInfo), rs_ci64(&r2, CIO_pDeskInfo));
    EXPECT_EQ(rs_ci64(&r1, CIO_flags), rs_ci64(&r2, CIO_flags));
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ti_fresh_threads_do_not_inherit_creator_value) {
    /* a thread created by a converted thread starts with Win32ThreadInfo of its own (TEB is zero-initialised) */
    struct rsnap m; rt_main_gui(&m);
    WK_PLAIN(w);
    if (w.before.ti) CHECK(w.before.ti != m.ti);
    EXPECT_NE(w.before.ti, m.ti);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ti_getkeystate_gate_uses_the_field) {
    /* user32!GetKeyState: if TEB+0x78 == 0 it asks NtUserGetThreadState(0xE) and returns 0 when that is 0; a converted
     * thread skips the call. Either way the result is the same state word, so two reads agree. */
    SHORT (WINAPI *gks)(int) = (SHORT (WINAPI *)(int))t_proc("GetKeyState");
    REQUIRE(gks);
    struct rsnap m; rt_main_gui(&m);
    CHECK(m.ti != 0);
    SHORT a = gks(VK_NUMLOCK), b = gks(VK_NUMLOCK);
    EXPECT_EQ(a & 1, b & 1);
}

/* ---- _CLIENTINFO on a fresh GUI worker ---------------------------------------------------------------- */

TEST(r2_teb_peb_field_matrix_ci_worker_flags_initialized_bit) {
    WK_GUI(w);
    CHECKF((rs_ci64(&w.after, CIO_flags) & CIF_INITIALIZED) != 0, "CI_flags=0x%llx", rs_ci64(&w.after, CIO_flags));
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_main_flags_initialized_bit) {
    struct rsnap m; rt_main_gui(&m);
    CHECKF((rs_ci64(&m, CIO_flags) & CIF_INITIALIZED) != 0, "CI_flags=0x%llx", rs_ci64(&m, CIO_flags));
}
TEST(r2_teb_peb_field_matrix_ci_flags_before_gui_have_no_initialized_bit) {
    WK_GUI_PRE(w);
    CHECK((rs_ci64(&w.before, CIO_flags) & CIF_INITIALIZED) == 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_registered_classes_pointer_in_user32) {
    /* ClientThreadSetup: TEB+0x8d0 = &gdwRegisteredClasses (a user32 data symbol) for every thread it sets up */
    WK_GUI(w);
    unsigned long long p = rs_ci64(&w.after, CIO_regClasses);
    CHECKF(rt_in_user32(p, 4), "lpdwRegisteredClasses=0x%llx", p);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_registered_classes_pointer_same_for_all_threads) {
    struct rsnap m; rt_main_gui(&m);
    WK_GUI(w);
    EXPECT_EQ(rs_ci64(&w.after, CIO_regClasses), rs_ci64(&m, CIO_regClasses));
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_registered_classes_zero_before_gui) {
    /* InitClientInfo writes 0; ClientThreadSetup fills it later */
    WK_GUI_PRE(w);
    EXPECT_EQ(rs_ci64(&w.before, CIO_regClasses), 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_pdeskinfo_user_pointer) {
    WK_GUI(w);
    unsigned long long p = rs_ci64(&w.after, CIO_pDeskInfo);
    CHECK(rt_user_va(p));
    unsigned char b[8];
    CHECK(k_try_read((void *)(ULONG_PTR)p, b, sizeof b));
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_pdeskinfo_equals_main) {
    struct rsnap m; rt_main_gui(&m);
    WK_GUI(w);
    CHECK(rs_ci64(&m, CIO_pDeskInfo) != 0);
    EXPECT_EQ(rs_ci64(&w.after, CIO_pDeskInfo), rs_ci64(&m, CIO_pDeskInfo));
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_deskinfo_first_qword_is_heap_base) {
    /* HMValidateHandle compares the handle entry's heap-base word with *pDeskInfo */
    struct rsnap m; rt_main_gui(&m);
    unsigned long long p = rs_ci64(&m, CIO_pDeskInfo), base = 0;
    REQUIRE(p != 0);
    CHECK(k_try_read((void *)(ULONG_PTR)p, &base, 8));
    CHECK(base != 0);
}
TEST(r2_teb_peb_field_matrix_ci_worker_client_delta_nonzero) {
    WK_GUI(w);
    CHECK(rs_ci64(&w.after, CIO_ulClientDelta) != 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_client_delta_equals_main) {
    struct rsnap m; rt_main_gui(&m);
    WK_GUI(w);
    EXPECT_EQ(rs_ci64(&w.after, CIO_ulClientDelta), rs_ci64(&m, CIO_ulClientDelta));
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_hkl_nonzero_and_equals_main) {
    struct rsnap m; rt_main_gui(&m);
    WK_GUI(w);
    CHECK(rs_ci64(&w.after, CIO_hKL) != 0);               /* InitClientInfo: pti->spklActive->hkl */
    EXPECT_EQ(rs_ci64(&w.after, CIO_hKL), rs_ci64(&m, CIO_hKL));
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_hkl_matches_kernel_query) {
    struct wk w;
    if (!wk_start(&w, 1, op_kbd, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    REQUIRE(w.x[0] != 0 || w.x[1] != 0);
    if (rs_ci64(&w.after, CIO_flags) & 0x4000) { t_note("CI_flags bit 14 set (IME override), skipping"); wk_finish(&w); return; }
    EXPECT_EQ(rs_ci64(&w.after, CIO_hKL), w.x[0]);        /* both come from the thread's active layout */
    EXPECT_EQ(w.x[0], w.x[1]);                              /* user32!GetKeyboardLayout(0) returns the syscall result */
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_hkl_low_word_is_a_langid) {
    WK_GUI(w);
    unsigned long long h = rs_ci64(&w.after, CIO_hKL);
    CHECK(h != 0);
    CHECK((h & 0xFFFF) != 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_hkl_zero_before_gui) {
    WK_GUI_PRE(w);
    EXPECT_EQ(rs_ci64(&w.before, CIO_hKL), 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_codepage_valid) {
    WK_GUI(w);
    unsigned cp = rs_ci16(&w.after, CIO_CodePage);
    t_note("CI.CodePage=%u", cp);
    if (cp) CHECK(IsValidCodePage(cp));
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_codepage_equals_main) {
    struct rsnap m; rt_main_gui(&m);
    WK_GUI(w);
    EXPECT_EQ(rs_ci16(&w.after, CIO_CodePage), rs_ci16(&m, CIO_CodePage));
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_expwinver_equals_main) {
    struct rsnap m; rt_main_gui(&m);
    WK_GUI(w);
    EXPECT_EQ(rs_ci32(&w.after, CIO_dwExpWinVer), rs_ci32(&m, CIO_dwExpWinVer));   /* both from the same image header */
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_expwinver_plausible) {
    struct rsnap m; rt_main_gui(&m);
    unsigned long v = rs_ci32(&m, CIO_dwExpWinVer);
    t_note("dwExpWinVer=0x%lx", v);
    CHECK(v >= 0x30A);                                    /* RtlGetExpWinVer never reports less than 3.10 */
    CHECK(v <= 0xA00 + 0xFF);
}
TEST(r2_teb_peb_field_matrix_ci_expwinver_not_set_before_gui) {
    WK_GUI_PRE(w);
    EXPECT_EQ(rs_ci32(&w.before, CIO_dwExpWinVer), 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_tiflags_reported) {
    WK_GUI(w);
    t_note("CI.dwTIFlags worker=0x%lx", rs_ci32(&w.after, CIO_dwTIFlags));
    struct rsnap m; rt_main_gui(&m);
    t_note("CI.dwTIFlags main=0x%lx", rs_ci32(&m, CIO_dwTIFlags));
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_compat_flags_reported) {
    struct rsnap m; rt_main_gui(&m);
    t_note("dwCompatFlags=0x%lx dwCompatFlags2=0x%lx", rs_ci32(&m, CIO_dwCompat), rs_ci32(&m, CIO_dwCompat2));
    t_info("cSpins=%llu", rs_ci64(&m, CIO_cSpins));
    WK_GUI(w);
    t_note("worker dwCompatFlags=0x%lx dwCompatFlags2=0x%lx", rs_ci32(&w.after, CIO_dwCompat), rs_ci32(&w.after, CIO_dwCompat2));
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_compat_flags_zero_before_gui) {
    WK_GUI_PRE(w);
    EXPECT_EQ(rs_ci32(&w.before, CIO_dwCompat), 0);
    EXPECT_EQ(rs_ci32(&w.before, CIO_dwCompat2), 0);
    EXPECT_EQ(rs_ci64(&w.before, CIO_cSpins), 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_msgdbcscb_clear) {
    /* user32!GetMessageA: a non-zero cached DBCS trail message sits at +0xa0..; a fresh thread has none */
    WK_GUI(w);
    CHECK(rs_ci_all_zero(&w.after, CIO_msgDbcsCB, CIO_msgDbcsEnd));
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_main_msgdbcscb_clear_when_idle) {
    struct rsnap m; rt_main_gui(&m);
    CHECK(rs_ci64(&m, CIO_msgDbcsCB + 0x10) == 0);        /* msgDbcsCB.wParam: no pending DBCS trail byte */
}
TEST(r2_teb_peb_field_matrix_ci_worker_ddeml_nesting_zero) {
    WK_GUI(w);
    EXPECT_EQ(rs_ci32(&w.after, CIO_cInDDEML), 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_no_hook_active) {
    /* user32!CallNextHookEx: phkCurrent == 0 means "not inside a hook procedure" */
    WK_GUI(w);
    EXPECT_EQ(rs_ci64(&w.after, CIO_phkCurrent), 0);
    EXPECT_EQ(rs_ci32(&w.after, CIO_dwHookCurrent), 0);
    EXPECT_EQ(rs_ci64(&w.after, CIO_dwHookData), 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_main_no_hook_active) {
    struct rsnap m; rt_main_gui(&m);
    EXPECT_EQ(rs_ci64(&m, CIO_phkCurrent), 0);
    LRESULT (WINAPI *cn)(HHOOK, int, WPARAM, LPARAM) = (LRESULT (WINAPI *)(HHOOK, int, WPARAM, LPARAM))t_proc("CallNextHookEx");
    REQUIRE(cn);
    EXPECT_EQ(cn(NULL, 0, 0, 0), 0);                      /* returns 0 straight away when phkCurrent == 0 */
    struct rsnap a; rs_take(&a);
    EXPECT_EQ(rs_ci64(&a, CIO_phkCurrent), 0);
}
TEST(r2_teb_peb_field_matrix_ci_worker_callbackwnd_cache_points_at_its_window) {
    /* user32!SendMessageW trusts CI+0x48 as the tagWND* whenever hwnd == CI+0x40; the head of a tagWND is its handle */
    WK_GUI(w);
    unsigned long long h = rs_ci64(&w.after, CIO_cbHwnd), p = rs_ci64(&w.after, CIO_cbPwnd), hh = 0;
    t_info("CallbackWnd hwnd=0x%llx pwnd=0x%llx", h, p);
    if (h && p && k_try_read((void *)(ULONG_PTR)p, &hh, 8)) EXPECT_EQ(hh, h);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_callbackwnd_zero_before_gui) {
    WK_GUI_PRE(w);
    EXPECT_EQ(rs_ci64(&w.before, CIO_cbHwnd), 0);
    EXPECT_EQ(rs_ci64(&w.before, CIO_cbPwnd), 0);
    EXPECT_EQ(rs_ci64(&w.before, CIO_cbActCtx), 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_worker_pcti_pointer) {
    WK_GUI(w);
    unsigned long long p = rs_ci64(&w.after, CIO_pcti);
    t_note("pClientThreadInfo=0x%llx teb+0x8e8=0x%llx", p, (unsigned long long)w.teb + TEB_Win32ClientInfo + CIO_ctiStore);
    if (p) {
        CHECK(rt_user_va(p));
        unsigned char b[0x10];
        CHECK(k_try_read((void *)(ULONG_PTR)p, b, sizeof b));
    }
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_pcti_outside_send_has_no_insend_bit) {
    /* user32!InSendMessage reads bit 1 of *pClientThreadInfo; outside a sent message it is clear */
    struct rsnap m; rt_main_gui(&m);
    unsigned long long p = rs_ci64(&m, CIO_pcti);
    BOOL (WINAPI *ism)(void) = (BOOL (WINAPI *)(void))t_proc("InSendMessage");
    REQUIRE(ism);
    EXPECT_EQ(ism(), 0);
    if (!p) { t_skip("no client thread info pointer"); return; }
    unsigned long f = 0;
    CHECK(k_try_read_u32((void *)(ULONG_PTR)p, &f));
    EXPECT_EQ(f & 2, 0);
}
TEST(r2_teb_peb_field_matrix_ci_pcti_distinct_between_threads) {
    struct rsnap m; rt_main_gui(&m);
    WK_GUI(w);
    unsigned long long a = rs_ci64(&m, CIO_pcti), b = rs_ci64(&w.after, CIO_pcti);
    if (!a || !b) { t_skip("no client thread info pointers"); wk_finish(&w); return; }
    CHECK(a != b);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ci_key_cache_reported) {
    struct rsnap m; rt_main_gui(&m);
    SHORT (WINAPI *gks)(int) = (SHORT (WINAPI *)(int))t_proc("GetKeyState");
    REQUIRE(gks);
    gks(VK_SHIFT); gks(VK_CAPITAL);
    struct rsnap a; rs_take(&a);
    t_info("dwKeyCache=0x%lx", rs_ci32(&a, CIO_dwKeyCache));
    CHECK(a.ok);
}
TEST(r2_teb_peb_field_matrix_ci_flags2_and_cti_store_reported) {
    struct rsnap m; rt_main_gui(&m);
    t_note("CI+0xe0=0x%llx CI+0xe8=0x%llx CI+0xf0=0x%llx", rs_ci64(&m, CIO_flags2), rs_ci64(&m, CIO_ctiStore), rs_ci64(&m, 0xf0));
    CHECK(m.ok);
}
TEST(r2_teb_peb_field_matrix_ci_worker_fields_survive_more_calls) {
    struct wk w;
    if (!wk_start(&w, 1, op_more_calls, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    EXPECT_EQ(w.x[2], w.x[3]);                              /* pDeskInfo */
    EXPECT_EQ(w.x[4], w.x[5]);                              /* ulClientDelta */
    EXPECT_EQ(w.x[6], w.x[7]);                              /* dwExpWinVer */
    wk_finish(&w);
}
