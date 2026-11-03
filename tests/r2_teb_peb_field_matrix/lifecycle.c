/* Thread lifecycle: threads created before/after the process connected, values inherited by new threads,
 * TEB state of parked and exited threads (read through NtQueryInformationThread's TebBaseAddress), and
 * what the kernel tears down when a GUI thread exits (windows die with their thread, GDI objects stay with
 * the process). */
#include "rtp.h"

static void join_keep(struct wk *w) {            /* release and join but keep the handle for queries */
    if (w->ev_go) SetEvent(w->ev_go);
    if (w->th) WaitForSingleObject(w->th, 8000);
}
static void close_all(struct wk *w) {
    if (w->th) CloseHandle(w->th);
    if (w->ev_ready) CloseHandle(w->ev_ready);
    if (w->ev_go) CloseHandle(w->ev_go);
    w->th = w->ev_ready = w->ev_go = NULL;
}

/* ---- sequential workers ----------------------------------------------------------------------------- */

TEST(r2_teb_peb_field_matrix_life_sequential_workers_start_clean_and_end_identical) {
    rt_gui();
    struct rsnap first; memset(&first, 0, sizeof first);
    ULONG_PTR tebs[8]; int repeats = 0;
    for (int i = 0; i < 8; i++) {
        struct wk w;
        if (!wk_start(&w, 1, NULL, NULL)) { wk_finish(&w); t_skip("worker %d did not start", i); return; }
        tebs[i] = w.teb;
        for (int j = 0; j < i; j++) if (tebs[j] == w.teb) repeats++;
        if (!w.before.ti) {                                   /* an exited thread's TEB memory may be reused, but never with stale state */
            CHECKF((rs_ci64(&w.before, CIO_flags) & CIF_INITIALIZED) == 0, "worker %d inherited CI_flags 0x%llx", i, rs_ci64(&w.before, CIO_flags));
            CHECKF(rs_ci64(&w.before, CIO_hKL) == 0, "worker %d inherited hKL", i);
            CHECKF(rs_ci64(&w.before, CIO_phkCurrent) == 0, "worker %d inherited phkCurrent", i);
        }
        CHECKF(w.after.ti != 0, "worker %d", i);
        if (i == 0) first = w.after;
        else {
            EXPECT_EQ(rs_ci64(&w.after, CIO_pDeskInfo), rs_ci64(&first, CIO_pDeskInfo));
            EXPECT_EQ(rs_ci64(&w.after, CIO_ulClientDelta), rs_ci64(&first, CIO_ulClientDelta));
            EXPECT_EQ(rs_ci64(&w.after, CIO_hKL), rs_ci64(&first, CIO_hKL));
            EXPECT_EQ(rs_ci32(&w.after, CIO_dwExpWinVer), rs_ci32(&first, CIO_dwExpWinVer));
            EXPECT_EQ(rs_ci64(&w.after, CIO_regClasses), rs_ci64(&first, CIO_regClasses));
        }
        wk_finish(&w);
    }
    t_note("TEB address reuse among 8 sequential workers: %d", repeats);
}
TEST(r2_teb_peb_field_matrix_life_sequential_workers_have_empty_gdi_batch) {
    rt_gui();
    for (int i = 0; i < 5; i++) {
        struct wk w;
        if (!wk_start(&w, 1, NULL, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
        EXPECT_EQ(w.after.gdi_count, 0);
        EXPECT_EQ(w.after.gdi_off, 0);
        wk_finish(&w);
    }
}

/* ---- concurrent workers ------------------------------------------------------------------------------ */

TEST(r2_teb_peb_field_matrix_life_sixteen_live_workers_distinct_and_consistent) {
    rt_gui();
    enum { N = 16 };
    static struct wk w[N];
    int started = 0;
    for (int i = 0; i < N; i++) { if (!wk_start(&w[i], 1, NULL, NULL)) { wk_finish(&w[i]); break; } started++; }
    CHECK(started >= 8);
    struct rsnap m; rs_take(&m);
    for (int i = 0; i < started; i++) {
        CHECKF(w[i].after.ti != 0, "worker %d has no thread info", i);
        EXPECT_EQ(rs_ci64(&w[i].after, CIO_pDeskInfo), rs_ci64(&m, CIO_pDeskInfo));
        EXPECT_EQ(rs_ci64(&w[i].after, CIO_ulClientDelta), rs_ci64(&m, CIO_ulClientDelta));
        EXPECT_EQ(rs_ci64(&w[i].after, CIO_hKL), rs_ci64(&m, CIO_hKL));
        EXPECT_EQ(rs_ci32(&w[i].after, CIO_dwExpWinVer), rs_ci32(&m, CIO_dwExpWinVer));
        for (int j = i + 1; j < started; j++) {
            CHECKF(w[i].after.ti != w[j].after.ti, "workers %d and %d share Win32ThreadInfo", i, j);
            CHECKF(w[i].teb != w[j].teb, "workers %d and %d share a TEB", i, j);
            CHECKF(rs_ci64(&w[i].after, CIO_pcti) != rs_ci64(&w[j].after, CIO_pcti) || rs_ci64(&w[i].after, CIO_pcti) == 0, "workers %d and %d share pClientThreadInfo", i, j);
        }
    }
    for (int i = 0; i < started; i++) wk_finish(&w[i]);
}
TEST(r2_teb_peb_field_matrix_life_live_worker_teb_stable_while_parked) {
    WK_GUI(w);
    struct rsnap a, b;
    rs_take_teb(&a, w.teb);
    Sleep(50);
    rs_take_teb(&b, w.teb);
    CHECK(a.ok && b.ok);
    EXPECT_EQ(a.ti, b.ti);
    CHECK(memcmp(a.ci, b.ci, CIO_ctiStore) == 0);              /* nothing user-visible changes in a thread that sits in a wait */
    EXPECT_EQ(a.gdi_count, b.gdi_count);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_life_remote_snapshot_equals_self_snapshot) {
    WK_GUI(w);
    struct rsnap r; rs_take_teb(&r, w.teb);
    EXPECT_EQ(r.pid, w.after.pid);
    EXPECT_EQ(r.tid, w.after.tid);
    EXPECT_EQ(r.ti, w.after.ti);
    CHECK(memcmp(r.ci, w.after.ci, CIO_ctiStore) == 0);
    EXPECT_EQ(r.gdi_count, w.after.gdi_count);
    EXPECT_EQ(r.gdi_tli, w.after.gdi_tli);
    wk_finish(&w);
}

/* ---- inherited values -------------------------------------------------------------------------------- */

static DWORD WINAPI nested_main(void *p) {
    struct rsnap *s = (struct rsnap *)p;
    rt_ts(0xE);
    rs_take(s);
    return 0;
}
static void op_nested(struct wk *w) {
    struct rsnap *s = (struct rsnap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *s);
    if (!s) return;
    HANDLE h = CreateThread(NULL, 0, nested_main, s, 0, NULL);
    if (h) { WaitForSingleObject(h, 5000); CloseHandle(h); }
    w->x[0] = s->ti; w->x[1] = rs_ci64(s, CIO_pDeskInfo); w->x[2] = rs_ci64(s, CIO_hKL); w->x[3] = rs_ci32(s, CIO_dwExpWinVer);
    w->x[4] = s->tid; w->x[5] = rs_ci64(s, CIO_ulClientDelta);
    HeapFree(GetProcessHeap(), 0, s);
}
TEST(r2_teb_peb_field_matrix_life_grandchild_thread_gets_same_desktop_state) {
    struct rsnap m; rt_main_gui(&m);
    struct wk w;
    if (!wk_start(&w, 1, op_nested, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    CHECK(w.x[0] != 0);
    CHECK(w.x[0] != w.after.ti);
    EXPECT_EQ(w.x[1], rs_ci64(&m, CIO_pDeskInfo));
    EXPECT_EQ(w.x[2], rs_ci64(&m, CIO_hKL));
    EXPECT_EQ(w.x[3], rs_ci32(&m, CIO_dwExpWinVer));
    EXPECT_EQ(w.x[5], rs_ci64(&m, CIO_ulClientDelta));
    CHECK(w.x[4] != w.tid);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_life_expwinver_does_not_depend_on_creating_thread) {
    struct rsnap m; rt_main_gui(&m);
    struct wk a, b;
    if (!wk_start(&a, 1, NULL, NULL)) { wk_finish(&a); t_skip("worker did not start"); return; }
    if (!wk_start(&b, 1, NULL, NULL)) { wk_finish(&a); wk_finish(&b); t_skip("worker did not start"); return; }
    EXPECT_EQ(rs_ci32(&a.after, CIO_dwExpWinVer), rs_ci32(&b.after, CIO_dwExpWinVer));
    EXPECT_EQ(rs_ci32(&a.after, CIO_dwExpWinVer), rs_ci32(&m, CIO_dwExpWinVer));
    wk_finish(&a); wk_finish(&b);
}

/* ---- exited threads ---------------------------------------------------------------------------------- */

TEST(r2_teb_peb_field_matrix_life_exited_thread_reports_exit_status_zero) {
    rt_gui();
    struct wk w;
    if (!wk_start(&w, 1, NULL, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    ULONG_PTR teb = rt_thread_teb(w.th);
    EXPECT_EQ(teb, w.teb);
    join_keep(&w);
    DWORD code = 0xFFFF;
    CHECK(GetExitCodeThread(w.th, &code));
    EXPECT_EQ(code, 0);
    rt_NtQIT_t f = (rt_NtQIT_t)rt_ntdll("NtQueryInformationThread");
    RT_TBI tbi; memset(&tbi, 0, sizeof tbi);
    LONG st = f ? f(w.th, 0, &tbi, sizeof tbi, NULL) : -1;
    if (st >= 0) {
        EXPECT_EQ(tbi.ExitStatus, 0);
        EXPECT_EQ(tbi.Tid, w.tid);
        t_note("exited thread TebBaseAddress=%p", tbi.TebBaseAddress);
    } else t_note("NtQueryInformationThread on an exited thread failed 0x%lx", (unsigned long)st);
    close_all(&w);
}
TEST(r2_teb_peb_field_matrix_life_exited_thread_teb_is_unmapped_or_reused) {
    rt_gui();
    struct wk w;
    if (!wk_start(&w, 1, NULL, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    ULONG_PTR teb = w.teb, tid = w.tid;
    join_keep(&w);
    close_all(&w);
    struct rsnap r; rs_take_teb(&r, teb);
    if (!r.ok) t_note("TEB of the exited thread is no longer readable");
    else {
        t_note("TEB of exited thread still readable: tid=%lu (was %lu) ti=0x%llx", r.tid, (unsigned long)tid, r.ti);
        if (r.tid == tid) t_note("the exited thread's TEB memory has not been recycled yet");
        else CHECK(r.tid != tid);                               /* recycled: it now belongs to another thread */
    }
}
TEST(r2_teb_peb_field_matrix_life_exited_thread_has_no_desktop) {
    rt_gui();
    REQUIRE(U(GetThreadDesktop));
    struct wk w;
    if (!wk_start(&w, 1, NULL, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    CHECK(U(GetThreadDesktop)(w.tid) != NULL);                    /* alive: has the process desktop */
    DWORD tid = w.tid;
    join_keep(&w);
    close_all(&w);
    SetLastError(0);
    HDESK d = U(GetThreadDesktop)(tid);
    EXPECT_EQ(d, NULL);                                        /* the thread no longer exists */
}

static void op_leave_window(struct wk *w) {
    HWND h = k_make_window("r2-life");
    w->x[0] = (unsigned long long)(ULONG_PTR)h;
    if (h) w->x[1] = U(IsWindow)(h);
}
static DWORD poll_gui(DWORD flag, DWORD want, DWORD ms) {
    DWORD t0 = GetTickCount(), v = 0;
    do { v = U(GetGuiResources)(GetCurrentProcess(), flag); if (v == want) break; Sleep(10); } while (GetTickCount() - t0 < ms);
    return v;
}
TEST(r2_teb_peb_field_matrix_life_thread_exit_destroys_its_windows) {
    rt_gui();
    REQUIRE(U(IsWindow)); REQUIRE(U(GetGuiResources));
    struct wk w;
    if (!wk_start(&w, 1, op_leave_window, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    HWND h = (HWND)(ULONG_PTR)w.x[0];
    REQUIRE(h != NULL);
    CHECK(w.x[1]);
    CHECK(U(IsWindow)(h));                                       /* alive while its thread is parked */
    wk_finish(&w);
    int gone = 0;
    for (int i = 0; i < 200 && !gone; i++) { gone = !U(IsWindow)(h); if (!gone) Sleep(10); }
    CHECK(gone);                                                 /* windows are destroyed with their owning thread */
}
TEST(r2_teb_peb_field_matrix_life_thread_exit_releases_user_objects) {
    rt_gui();
    REQUIRE(U(GetGuiResources));
    DWORD base = U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS);
    struct wk w;
    if (!wk_start(&w, 1, op_leave_window, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    DWORD during = U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS);
    CHECK(during > base);
    wk_finish(&w);
    DWORD after = poll_gui(GR_USEROBJECTS, base, 3000);
    EXPECT_EQ(after, base);
}

static void op_leak_dc(struct wk *w) {
    HDC d = U(CreateCompatibleDC)(NULL);
    w->x[0] = (unsigned long long)(ULONG_PTR)d;
}
TEST(r2_teb_peb_field_matrix_life_gdi_objects_belong_to_the_process_not_the_thread) {
    rt_gui();
    REQUIRE(U(GetGuiResources)); REQUIRE(U(DeleteDC));
    DWORD base = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    struct wk w;
    if (!wk_start(&w, 1, op_leak_dc, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    HDC d = (HDC)(ULONG_PTR)w.x[0];
    REQUIRE(d != NULL);
    wk_finish(&w);
    Sleep(50);
    EXPECT_EQ(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS), base + 1);   /* survives its creating thread */
    CHECK(U(DeleteDC)(d));                                                          /* and can be deleted from another thread */
    EXPECT_EQ(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS), base);
}
static void op_exit_with_batch(struct wk *w) {
    HDC d = U(CreateCompatibleDC)(NULL);
    HBITMAP b = U(CreateBitmap)(4, 4, 1, 32, NULL);
    if (d && b) { U(SelectObject)(d, b); U(SetBrushOrgEx)(d, 3, 5, NULL); }
    struct rsnap s; rs_take(&s);
    w->x[0] = s.gdi_count;
    w->x[1] = (unsigned long long)(ULONG_PTR)d;
    w->x[2] = (unsigned long long)(ULONG_PTR)b;
}
TEST(r2_teb_peb_field_matrix_life_exit_with_pending_batch_leaves_process_usable) {
    rt_gui();
    struct wk w;
    if (!wk_start(&w, 1, op_exit_with_batch, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    HDC d = (HDC)(ULONG_PTR)w.x[1]; HBITMAP b = (HBITMAP)(ULONG_PTR)w.x[2];
    t_note("worker exited with %llu pending batch records", w.x[0]);
    wk_finish(&w);
    struct rsnap m; rs_take(&m);
    EXPECT_EQ(m.gdi_count, 0);                                   /* the batch is in the exited thread's TEB, not ours */
    HDC mine = U(CreateCompatibleDC)(NULL);
    CHECK(mine != NULL);
    if (mine) U(DeleteDC)(mine);
    if (d) U(DeleteDC)(d);
    if (b) U(DeleteObject)(b);
}
