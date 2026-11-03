/* GDI members of the TEB: GdiTebBatch (Offset/HasRenderingCommand, HDC, Buffer), GdiBatchCount,
 * GdiCachedProcessHandle, GdiClientPID/TID, GdiThreadLocalInfo and the OpenGL block (glDispatchTable ..
 * glContext) that only opengl32 touches.
 *
 * gdi32full!SetBrushOrgEx queues a 0x10-byte record {USHORT len; USHORT type=4; x; y} when the origin changes
 * (see gdi_dcattr_batching for the full record catalogue); every queued record bumps TEB.GdiBatchCount and
 * advances GdiTebBatch.Offset by the 8-aligned length. The kernel drains the batch on entry to any win32k
 * service (KiSystemServiceStart tests GdiBatchCount) and in NtGdiFlush. */
#include "rtp.h"

#define BT_OFFSET_MASK 0x7FFFFFFFul
#define BT_HASRENDER   0x80000000ul

struct fx { HDC dc; HBITMAP bmp, old; DWORD lim; };
static int g_org;

static int fx_open(struct fx *f) {
    memset(f, 0, sizeof *f);
    if (!U(CreateCompatibleDC) || !U(CreateBitmap) || !U(SelectObject) || !U(GdiFlush)) return 0;
    f->dc = U(CreateCompatibleDC)(NULL);
    if (!f->dc) return 0;
    f->bmp = U(CreateBitmap)(8, 8, 1, 32, NULL);
    if (!f->bmp) { U(DeleteDC)(f->dc); f->dc = NULL; return 0; }
    f->old = (HBITMAP)U(SelectObject)(f->dc, f->bmp);
    if (U(GdiGetBatchLimit)) f->lim = U(GdiGetBatchLimit)();
    U(GdiFlush)();
    return 1;
}
static void fx_close(struct fx *f) {
    if (!f->dc) return;
    U(GdiFlush)();
    if (U(GdiSetBatchLimit) && f->lim >= 1 && f->lim <= 20) U(GdiSetBatchLimit)(f->lim);
    if (f->old) U(SelectObject)(f->dc, f->old);
    U(DeleteObject)(f->bmp);
    U(DeleteDC)(f->dc);
    f->dc = NULL;
}
#define FX(f) struct fx f; if (!fx_open(&f)) { t_skip("no memory DC"); return; }
static BOOL org(struct fx *f) { g_org += 2; return U(SetBrushOrgEx)(f->dc, g_org, g_org + 1, NULL); }
static unsigned long b_count(void) { struct rsnap s; rs_take(&s); return s.gdi_count; }
static unsigned long b_off(void) { struct rsnap s; rs_take(&s); return s.gdi_off & BT_OFFSET_MASK; }
#define NEED_BATCH(f) do { if (b_count() == 0) { t_note("SetBrushOrgEx was not queued (no batching in this configuration)"); fx_close(&f); t_skip("no batching"); return; } } while (0)

/* ---- the empty batch ------------------------------------------------------------------------------ */

TEST(r2_teb_peb_field_matrix_gdi_worker_batch_empty_after_gui) {
    WK_GUI(w);
    EXPECT_EQ(w.after.gdi_count, 0);
    EXPECT_EQ(w.after.gdi_off, 0);
    EXPECT_EQ(w.after.gdi_hdc, 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_gdi_main_flush_leaves_empty_batch) {
    FX(f);
    org(&f);
    CHECK(U(GdiFlush)());
    EXPECT_EQ(b_count(), 0);
    EXPECT_EQ(b_off(), 0);
    fx_close(&f);
}
TEST(r2_teb_peb_field_matrix_gdi_flush_on_empty_batch_is_true_and_stays_empty) {
    FX(f);
    CHECK(U(GdiFlush)());
    CHECK(U(GdiFlush)());
    EXPECT_EQ(b_count(), 0);
    EXPECT_EQ(b_off(), 0);
    fx_close(&f);
}

/* ---- one queued record ---------------------------------------------------------------------------- */

TEST(r2_teb_peb_field_matrix_gdi_brushorg_bumps_count_offset_and_hdc) {
    FX(f);
    CHECK(org(&f));
    NEED_BATCH(f);
    struct rsnap s; rs_take(&s);
    EXPECT_EQ(s.gdi_count, 1);
    EXPECT_EQ(s.gdi_off & BT_OFFSET_MASK, 0x10);
    EXPECT_EQ(s.gdi_hdc, (unsigned long long)(ULONG_PTR)f.dc);
    fx_close(&f);
}
TEST(r2_teb_peb_field_matrix_gdi_brushorg_record_header_and_payload) {
    FX(f);
    CHECK(org(&f));
    NEED_BATCH(f);
    unsigned char rec[16];
    CHECK(k_try_read((char *)k_teb() + TEB_GdiTebBatch + GDI_TEB_BATCH_Buffer, rec, sizeof rec));
    EXPECT_EQ(*(unsigned short *)(rec + 0), 0x10);        /* length */
    EXPECT_EQ(*(unsigned short *)(rec + 2), 4);           /* record type: brush origin */
    EXPECT_EQ(*(unsigned long *)(rec + 4), (unsigned long)g_org);
    EXPECT_EQ(*(unsigned long *)(rec + 8), (unsigned long)(g_org + 1));
    fx_close(&f);
}
TEST(r2_teb_peb_field_matrix_gdi_brushorg_does_not_set_rendering_bit) {
    FX(f);
    org(&f);
    NEED_BATCH(f);
    struct rsnap s; rs_take(&s);
    EXPECT_EQ(s.gdi_off & BT_HASRENDER, 0);                /* only drawing records (type < 4) set HasRenderingCommand */
    fx_close(&f);
}
TEST(r2_teb_peb_field_matrix_gdi_patblt_sets_rendering_bit) {
    FX(f);
    CHECK(U(PatBlt)(f.dc, 0, 0, 4, 4, BLACKNESS));
    NEED_BATCH(f);
    struct rsnap s; rs_take(&s);
    EXPECT_EQ(s.gdi_off & BT_HASRENDER, BT_HASRENDER);
    EXPECT_EQ(s.gdi_hdc, (unsigned long long)(ULONG_PTR)f.dc);
    unsigned char rec[4];
    CHECK(k_try_read((char *)k_teb() + TEB_GdiTebBatch + GDI_TEB_BATCH_Buffer, rec, sizeof rec));
    EXPECT_EQ(*(unsigned short *)(rec + 2), 0);            /* PatBlt record type 0 */
    EXPECT_EQ(*(unsigned short *)(rec + 0), 0x48);
    CHECK(U(GdiFlush)());
    EXPECT_EQ(b_count(), 0);
    EXPECT_EQ(b_off(), 0);                                 /* flush clears Offset including the rendering bit */
    fx_close(&f);
}
TEST(r2_teb_peb_field_matrix_gdi_records_accumulate_count_and_offset) {
    FX(f);
    org(&f);
    NEED_BATCH(f);
    for (int i = 0; i < 4; i++) org(&f);
    EXPECT_EQ(b_count(), 5);
    EXPECT_EQ(b_off(), 0x50);
    fx_close(&f);
}
TEST(r2_teb_peb_field_matrix_gdi_same_origin_again_queues_nothing) {
    FX(f);
    org(&f);
    NEED_BATCH(f);
    CHECK(U(SetBrushOrgEx)(f.dc, g_org, g_org + 1, NULL));  /* unchanged origin returns early */
    EXPECT_EQ(b_count(), 1);
    EXPECT_EQ(b_off(), 0x10);
    fx_close(&f);
}

/* ---- draining ------------------------------------------------------------------------------------ */

TEST(r2_teb_peb_field_matrix_gdi_ntgdiflush_stub_clears_count_and_offset) {
    NtGdiFlush_pfn flush = (NtGdiFlush_pfn)t_proc_in("win32u.dll", "NtGdiFlush");
    REQUIRE(flush);
    FX(f);
    org(&f);
    NEED_BATCH(f);
    flush();
    EXPECT_EQ(b_count(), 0);
    EXPECT_EQ(b_off(), 0);
    fx_close(&f);
}
TEST(r2_teb_peb_field_matrix_gdi_any_win32k_service_drains_the_batch) {
    FX(f);
    org(&f);
    NEED_BATCH(f);
    rt_ts(0xE);                                            /* KiSystemServiceStart flushes before running the service */
    EXPECT_EQ(b_count(), 0);
    EXPECT_EQ(b_off(), 0);
    fx_close(&f);
}
TEST(r2_teb_peb_field_matrix_gdi_user_api_drains_the_batch) {
    FX(f);
    org(&f);
    NEED_BATCH(f);
    POINT p; U(GetCursorPos)(&p);
    EXPECT_EQ(b_count(), 0);
    fx_close(&f);
}
TEST(r2_teb_peb_field_matrix_gdi_batch_limit_one_never_leaves_pending_records) {
    FX(f);
    REQUIRE(U(GdiSetBatchLimit));
    U(GdiSetBatchLimit)(1);
    org(&f); org(&f);
    EXPECT_EQ(b_count(), 0);
    EXPECT_EQ(b_off(), 0);
    fx_close(&f);
}
TEST(r2_teb_peb_field_matrix_gdi_batch_limit_caps_pending_count) {
    FX(f);
    REQUIRE(U(GdiSetBatchLimit));
    U(GdiSetBatchLimit)(3);
    for (int i = 0; i < 10; i++) { org(&f); CHECKF(b_count() <= 3, "count=%lu after %d records", b_count(), i + 1); }
    fx_close(&f);
}
TEST(r2_teb_peb_field_matrix_gdi_hdc_field_tracks_last_queued_dc) {
    FX(f);
    struct fx g;
    if (!fx_open(&g)) { fx_close(&f); t_skip("no second DC"); return; }
    org(&f);
    NEED_BATCH(f);
    struct rsnap s; rs_take(&s);
    EXPECT_EQ(s.gdi_hdc, (unsigned long long)(ULONG_PTR)f.dc);
    org(&g);                                              /* a record for another DC forces the first batch out first */
    rs_take(&s);
    if (s.gdi_count) EXPECT_EQ(s.gdi_hdc, (unsigned long long)(ULONG_PTR)g.dc);
    fx_close(&g); fx_close(&f);
}

/* ---- per-thread isolation of the batch ------------------------------------------------------------ */

TEST(r2_teb_peb_field_matrix_gdi_batch_is_per_thread) {
    FX(f);
    org(&f);
    NEED_BATCH(f);
    struct rsnap mine; rs_take(&mine);
    WK_GUI(w);                                            /* the worker's own win32k calls must not touch our pending batch */
    EXPECT_EQ(w.after.gdi_count, 0);
    struct rsnap again; rs_take(&again);
    EXPECT_EQ(again.gdi_count, mine.gdi_count);
    EXPECT_EQ(again.gdi_off, mine.gdi_off);
    struct rsnap remote; rs_take_teb(&remote, (ULONG_PTR)k_teb());
    EXPECT_EQ(remote.gdi_count, mine.gdi_count);
    wk_finish(&w);
    fx_close(&f);
}
static void op_queue_in_worker(struct wk *w) {
    struct fx f;
    if (!fx_open(&f)) return;
    org(&f);
    struct rsnap s; rs_take(&s);
    w->x[0] = s.gdi_count; w->x[1] = s.gdi_off & BT_OFFSET_MASK; w->x[2] = s.gdi_hdc; w->x[3] = (unsigned long long)(ULONG_PTR)f.dc;
    fx_close(&f);
    rs_take(&s);
    w->x[4] = s.gdi_count; w->x[5] = s.gdi_off;
}
TEST(r2_teb_peb_field_matrix_gdi_worker_batch_does_not_leak_into_main) {
    struct wk w;
    rt_gui();
    if (!wk_start(&w, 1, op_queue_in_worker, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    if (w.x[0] == 0) { wk_finish(&w); t_note("worker queued nothing"); t_skip("no batching"); return; }
    EXPECT_EQ(w.x[0], 1);
    EXPECT_EQ(w.x[1], 0x10);
    EXPECT_EQ(w.x[2], w.x[3]);
    EXPECT_EQ(w.x[4], 0);                                  /* fx_close flushed it */
    EXPECT_EQ(w.x[5], 0);
    EXPECT_EQ(b_count(), 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_gdi_remote_read_of_parked_worker_shows_its_empty_batch) {
    WK_GUI(w);
    struct rsnap r; rs_take_teb(&r, w.teb);
    EXPECT_EQ(r.gdi_count, 0);
    EXPECT_EQ(r.gdi_off, 0);
    wk_finish(&w);
}

/* ---- other GDI members of the TEB ----------------------------------------------------------------- */

TEST(r2_teb_peb_field_matrix_gdi_client_pid_tid_match_when_set) {
    struct rsnap m; rt_main_gui(&m); U(GdiFlush)();
    rs_take(&m);
    t_info("GdiClientPID=%lu GdiClientTID=%lu", m.gdi_pid, m.gdi_tid);
    if (m.gdi_pid) EXPECT_EQ(m.gdi_pid, GetCurrentProcessId());
    if (m.gdi_tid) EXPECT_EQ(m.gdi_tid, GetCurrentThreadId());
}
TEST(r2_teb_peb_field_matrix_gdi_worker_client_ids_match_when_set) {
    WK_GUI(w);
    if (w.after.gdi_pid) EXPECT_EQ(w.after.gdi_pid, GetCurrentProcessId());
    if (w.after.gdi_tid) EXPECT_EQ(w.after.gdi_tid, w.tid);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_gdi_cached_process_handle_names_this_process_when_set) {
    FX(f);
    org(&f); U(GdiFlush)();
    struct rsnap m; rs_take(&m);
    t_note("GdiCachedProcessHandle=0x%llx", m.gdi_cached);
    if (m.gdi_cached) {
        DWORD pid = GetProcessId((HANDLE)(ULONG_PTR)m.gdi_cached);
        if (pid) EXPECT_EQ(pid, GetCurrentProcessId());
        else t_note("cached handle does not resolve to a process id");
    }
    fx_close(&f);
}
TEST(r2_teb_peb_field_matrix_gdi_cached_process_handle_stable) {
    struct rsnap a, b; rt_main_gui(&a);
    HDC d = U(CreateCompatibleDC)(NULL); if (d) U(DeleteDC)(d);
    rs_take(&b);
    EXPECT_EQ(a.gdi_cached, b.gdi_cached);
}
TEST(r2_teb_peb_field_matrix_gdi_thread_local_info_per_thread) {
    struct rsnap m; rt_main_gui(&m);
    WK_GUI(w);
    t_note("GdiThreadLocalInfo main=0x%llx worker=0x%llx", m.gdi_tli, w.after.gdi_tli);
    if (m.gdi_tli && w.after.gdi_tli) CHECK(m.gdi_tli != w.after.gdi_tli);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_gdi_thread_local_info_zero_before_gui) {
    WK_GUI_PRE(w);
    EXPECT_EQ(w.before.gdi_tli, 0);
    EXPECT_EQ(w.before.gdi_cached, 0);
    EXPECT_EQ(w.before.gdi_pid, 0);
    EXPECT_EQ(w.before.gdi_tid, 0);
    wk_finish(&w);
}

/* ---- OpenGL block --------------------------------------------------------------------------------- */

static int gl_block_zero(ULONG_PTR teb) {
    unsigned char buf[TEB_glReserved1 - TEB_glDispatchTable];   /* glDispatchTable[233] only; glReserved1/2 are opengl32 private */
    if (!k_try_read((void *)(teb + TEB_glDispatchTable), buf, sizeof buf)) return -1;
    for (unsigned i = 0; i < sizeof buf; i++) if (buf[i]) return 0;
    return 1;
}
TEST(r2_teb_peb_field_matrix_gl_block_zero_on_main_without_opengl32) {
    rt_gui();
    if (GetModuleHandleA("opengl32.dll")) { t_skip("opengl32 loaded"); return; }
    EXPECT_EQ(gl_block_zero((ULONG_PTR)k_teb()), 1);       /* glDispatchTable .. glContext are written by opengl32 only */
}
TEST(r2_teb_peb_field_matrix_gl_block_zero_on_worker_without_opengl32) {
    if (GetModuleHandleA("opengl32.dll")) { t_skip("opengl32 loaded"); return; }
    WK_GUI(w);
    EXPECT_EQ(gl_block_zero(w.teb), 1);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_gl_scalar_members_zero_without_opengl32) {
    rt_gui();
    if (GetModuleHandleA("opengl32.dll")) { t_skip("opengl32 loaded"); return; }
    struct rsnap m; rs_take(&m);
    EXPECT_EQ(m.gl_sectioninfo, 0);
    EXPECT_EQ(m.gl_section, 0);
    EXPECT_EQ(m.gl_table, 0);
    EXPECT_EQ(m.gl_currentrc, 0);
    EXPECT_EQ(m.gl_context, 0);
}
TEST(r2_teb_peb_field_matrix_gl_block_offsets_are_inside_the_teb) {
    CHECK(TEB_glDispatchTable + 233 * 8 <= TEB_glReserved1);   /* glDispatchTable[233] ends where glReserved1 starts */
    EXPECT_EQ(TEB_glReserved1, 0x1138);
    EXPECT_EQ(TEB_glSectionInfo, 0x1228);
    CHECK(TEB_glContext + 8 <= TEB_LastStatusValue);
}
