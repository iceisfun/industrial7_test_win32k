/* The GDI call batch: TEB.GdiTebBatch (Offset/HasRenderingCommand, HDC, Buffer) and TEB.GdiBatchCount.
 *
 * gdi32full queues a few calls instead of making a syscall: SetBrushOrgEx (record type 4, 0x10 bytes),
 * SelectObject(font) (type 6, 0x10), TextOutW/ExtTextOut text (type 2, variable), ExtTextOut with only an
 * opaque rect (type 3, 0x28) and PatBlt (type 0, 0x48). Every queued call bumps GdiBatchCount, advances
 * Offset by the 8-aligned record length, records the DC in GdiTebBatch.HDC and sets a pending bit in
 * the DC_ATTR page. The kernel drains the batch on entry to every win32k service (ntoskrnl
 * KiSystemServiceStart tests TEB.GdiBatchCount for the shadow service table) and in NtGdiFlush.
 *
 * Record headers are {USHORT Length; USHORT Type;} at Buffer+Offset, as written by the gdi32full code. */
#include "dcattr.h"

static int g_n;
/* Always a change of the brush origin (the call only queues when the origin differs from the page). */
static BOOL bo(struct fx *f) { g_n += 2; return U(SetBrushOrgEx)(f->dc, g_n, g_n + 1, NULL); }

static DWORD g_entry_limit;
static void lim_save(void) { g_entry_limit = U(GdiGetBatchLimit)(); }
static void lim_restore(void) { U(GdiSetBatchLimit)(g_entry_limit >= 1 && g_entry_limit <= 20 ? g_entry_limit : 20); }

#define FLUSHED() do { EXPECT_EQ(teb_count(), 0); EXPECT_EQ(teb_offset(), 0); } while (0)

/* ---- the empty batch --------------------------------------------------------------------------- */

TEST(gdi_dcattr_batching_batch_is_empty_after_flush) {
    FXT(f, 8, 8);
    batch_reset();
    FLUSHED();
    EXPECT_EQ(teb_hrc(), 0);
    EXPECT_EQ(teb_hdc(), NULL);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_gdiflush_returns_true_when_empty) {
    FXT(f, 8, 8);
    CHECK(U(GdiFlush)());
    CHECK(U(GdiFlush)());
    FLUSHED();
    fx_close(&f);
}

TEST(gdi_dcattr_batching_default_limit_in_pristine_process) {
    struct k_child_result r = k_child_run("gdi_dcattr_batching_child_default_limit", "", 20000);
    EXPECT_EQ(r.exit_code, 0);
}

CHILD(gdi_dcattr_batching_child_default_limit) {
    /* A process that has done no batching yet: GdiBatchLimit is its static initial value, 0x14 (20). */
    int bad = 0;
    if (!U(GdiGetBatchLimit)) { t_skip("no GdiGetBatchLimit"); return 0; }
    DWORD d = U(GdiGetBatchLimit)();
    if (d != 20) { printf("  FAIL default GdiGetBatchLimit = %lu, want 20\n", d); bad++; }
    if (teb_count() != 0 || teb_offset() != 0) { printf("  FAIL fresh process has a non-empty batch\n"); bad++; }
    return bad;
}

/* ---- SetBrushOrgEx records ------------------------------------------------------------------- */

TEST(gdi_dcattr_batching_brushorg_queues_one_record) {
    FXT(f, 8, 8);
    CHECK(bo(&f));
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    EXPECT_EQ(teb_count(), 1);
    EXPECT_EQ(teb_offset(), 0x10);
    EXPECT_EQ(teb_hdc(), f.dc);
    EXPECT_EQ(rec_len(0), 0x10);
    EXPECT_EQ(rec_type(0), REC_BRUSHORG);
    EXPECT_EQ(rd32(teb_rec(0) + 4), (unsigned long)g_n);
    EXPECT_EQ(rd32(teb_rec(0) + 8), (unsigned long)(g_n + 1));
    EXPECT_EQ(teb_hrc(), 0);                         /* only record types < 4 set HasRenderingCommand */
    fx_close(&f);
}

TEST(gdi_dcattr_batching_brushorg_records_accumulate) {
    FXT(f, 8, 8);
    bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    int first = g_n;
    bo(&f); bo(&f);
    EXPECT_EQ(teb_count(), 3);
    EXPECT_EQ(teb_offset(), 0x30);
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(rec_len(i * 0x10), 0x10);
        EXPECT_EQ(rec_type(i * 0x10), REC_BRUSHORG);
        EXPECT_EQ(rd32(teb_rec(i * 0x10) + 4), (unsigned long)(first + 2 * i));
    }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_brushorg_same_value_is_not_queued) {
    FXT(f, 8, 8);
    bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    /* SetBrushOrgEx returns 1 early when the page already holds (x, y): nothing is queued. */
    CHECK(U(SetBrushOrgEx)(f.dc, g_n, g_n + 1, NULL));
    EXPECT_EQ(teb_count(), 1);
    EXPECT_EQ(teb_offset(), 0x10);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_brushorg_page_is_ahead_of_kernel_until_flush) {
    FXT(f, 8, 8); NEEDA(f);
    bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    POINT p = dca_pt(f.a, DCA_BRUSHORG);
    EXPECT_EQ(p.x, g_n); EXPECT_EQ(p.y, g_n + 1);
    batch_reset();
    p = dca_pt(f.a, DCA_BRUSHORG);                  /* flush replays the record; the page keeps the value */
    EXPECT_EQ(p.x, g_n); EXPECT_EQ(p.y, g_n + 1);
    fx_close(&f);
}

/* ---- draining -------------------------------------------------------------------------------- */

TEST(gdi_dcattr_batching_gdiflush_drains_pending_batch) {
    FXT(f, 8, 8);
    bo(&f); bo(&f); bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    CHECK(U(GdiFlush)());
    FLUSHED();
    EXPECT_EQ(teb_hrc(), 0);
    if (teb_hdc() != NULL) t_note("TEB.GdiTebBatch.HDC left at %p after GdiFlush (DC %p)", teb_hdc(), f.dc);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_ntgdiflush_stub_drains_pending_batch) {
    UDECL(NTSTATUS, NtGdiFlush, (void));
    REQUIRE(UN(NtGdiFlush));
    FXT(f, 8, 8);
    bo(&f); bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    UN(NtGdiFlush)();
    FLUSHED();
    fx_close(&f);
}

TEST(gdi_dcattr_batching_raw_syscall_ntgdiflush_drains_pending_batch) {
    UDECL(NTSTATUS, NtGdiFlush, (void));
    REQUIRE(UN(NtGdiFlush));
    if (!k_stub_valid((FARPROC)UN(NtGdiFlush))) { t_skip("NtGdiFlush stub shape unknown"); return; }
    FXT(f, 8, 8);
    bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    long num = k_stub_number((FARPROC)UN(NtGdiFlush));
    k_raw_syscall((unsigned long)num, 0, 0, 0, 0, 0, 0, 0, 0);
    FLUSHED();
    fx_close(&f);
}

UDECL(HWND, NtUserGetForegroundWindow, (void));

static void drain_by(const char *name, void (*call)(HDC)) {
    FXT(f, 8, 8);
    bo(&f);
    if (teb_count() == 0) { t_note("batch not observed before %s", name); fx_close(&f); return; }
    call(f.dc);
    CHECKF(teb_count() == 0 && teb_offset() == 0, "%s did not drain the batch: count %u offset 0x%x", name, teb_count(), teb_offset());
    fx_close(&f);
}
static void c_fgwnd(HDC d) { (void)d; U(GetForegroundWindow)(); }
static void c_cursor(HDC d) { POINT p; (void)d; U(GetCursorPos)(&p); }
static void c_keystate(HDC d) { (void)d; U(GetKeyState)(VK_SHIFT); }
static void c_asynckey(HDC d) { (void)d; U(GetAsyncKeyState)(VK_SHIFT); }
static void c_capture(HDC d) { (void)d; U(GetCapture)(); }
static void c_getpixel(HDC d) { U(GetPixel)(d, 0, 0); }
static void c_createdc(HDC d) { HDC x = U(CreateCompatibleDC)(d); if (x) U(DeleteDC)(x); }
static void c_clipbox(HDC d) { RECT r; U(GetClipBox)(d, &r); }
static void c_stub_fg(HDC d) { (void)d; if (UN(NtUserGetForegroundWindow)) UN(NtUserGetForegroundWindow)(); }

/* KiSystemServiceStart: for the win32k service table (index 0x20 in the dispatch) a non-zero
 * TEB.GdiBatchCount makes the kernel run the flush callout before the service body. */
TEST(gdi_dcattr_batching_win32k_syscall_getforegroundwindow_drains) { REQUIRE(U(GetForegroundWindow)); drain_by("GetForegroundWindow", c_fgwnd); }
TEST(gdi_dcattr_batching_win32k_syscall_getcursorpos_drains) { REQUIRE(U(GetCursorPos)); drain_by("GetCursorPos", c_cursor); }
TEST(gdi_dcattr_batching_win32k_syscall_getkeystate_drains) { REQUIRE(U(GetKeyState)); drain_by("GetKeyState", c_keystate); }
TEST(gdi_dcattr_batching_win32k_syscall_getasynckeystate_drains) { REQUIRE(U(GetAsyncKeyState)); drain_by("GetAsyncKeyState", c_asynckey); }
TEST(gdi_dcattr_batching_win32k_syscall_getcapture_drains) { REQUIRE(U(GetCapture)); drain_by("GetCapture", c_capture); }
TEST(gdi_dcattr_batching_win32k_syscall_getpixel_drains) { REQUIRE(U(GetPixel)); drain_by("GetPixel", c_getpixel); }
TEST(gdi_dcattr_batching_win32k_syscall_createcompatibledc_drains) { drain_by("CreateCompatibleDC", c_createdc); }
TEST(gdi_dcattr_batching_win32k_syscall_getclipbox_drains) { REQUIRE(U(GetClipBox)); drain_by("GetClipBox", c_clipbox); }
TEST(gdi_dcattr_batching_win32k_syscall_win32u_stub_drains) { REQUIRE(UN(NtUserGetForegroundWindow)); drain_by("win32u!NtUserGetForegroundWindow", c_stub_fg); }

TEST(gdi_dcattr_batching_non_win32k_syscalls_leave_batch_pending) {
    FXT(f, 8, 8);
    bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    unsigned c0 = teb_count(), o0 = teb_offset();
    /* Only the win32k service table consults GdiBatchCount (KiSystemServiceStart: cmp edi,0x20), so
     * ordinary ntoskrnl syscalls reached through kernel32/ntdll must not disturb the batch. */
    Sleep(0);
    GetTickCount();
    GetCurrentProcessId();
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (ev) { SetEvent(ev); WaitForSingleObject(ev, 1000); CloseHandle(ev); }
    GetCurrentThread();
    SetLastError(5);
    EXPECT_EQ(teb_count(), c0);
    EXPECT_EQ(teb_offset(), o0);
    EXPECT_EQ(teb_hdc(), f.dc);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_page_setters_do_not_touch_pending_batch) {
    FXT(f, 8, 8);
    bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    /* SetBkColor/SetTextColor/SetBkMode/SetTextAlign/MoveToEx only write the page: no syscall, no flush. */
    U(SetBkColor)(f.dc, 0x00102030);
    U(SetTextColor)(f.dc, 0x00304050);
    U(SetBkMode)(f.dc, TRANSPARENT);
    U(SetTextAlign)(f.dc, TA_BASELINE);
    U(MoveToEx)(f.dc, 3, 3, NULL);
    U(GetBkColor)(f.dc); U(GetROP2)(f.dc); U(GetMapMode)(f.dc);
    EXPECT_EQ(teb_count(), 1);
    EXPECT_EQ(teb_offset(), 0x10);
    EXPECT_EQ(teb_hdc(), f.dc);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_selecting_brush_does_not_flush) {
    FXT(f, 8, 8);
    bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    U(SelectObject)(f.dc, U(GetStockObject)(BLACK_BRUSH));    /* page write only (SelectObjectImpl brush case) */
    U(SelectObject)(f.dc, U(GetStockObject)(BLACK_PEN));
    EXPECT_EQ(teb_count(), 1);
    fx_close(&f);
}

/* ---- the limit ------------------------------------------------------------------------------- */

TEST(gdi_dcattr_batching_limit_return_values) {
    lim_save();
    REQUIRE(U(GdiSetBatchLimit));
    DWORD r;
    r = U(GdiSetBatchLimit)(7);                 /* in 1..20: stored, previous returned */
    EXPECT_EQ(r, g_entry_limit);
    EXPECT_EQ(U(GdiGetBatchLimit)(), 7);
    EXPECT_EQ(U(GdiSetBatchLimit)(9), 7);
    EXPECT_EQ(U(GdiGetBatchLimit)(), 9);
    EXPECT_EQ(U(GdiSetBatchLimit)(0), 9);       /* 0 means "reset to the default 0x14" */
    EXPECT_EQ(U(GdiGetBatchLimit)(), 20);
    EXPECT_EQ(U(GdiSetBatchLimit)(1), 20);
    EXPECT_EQ(U(GdiGetBatchLimit)(), 1);
    EXPECT_EQ(U(GdiSetBatchLimit)(20), 1);
    EXPECT_EQ(U(GdiGetBatchLimit)(), 20);
    lim_restore();
}

TEST(gdi_dcattr_batching_limit_out_of_range_is_rejected) {
    lim_save();
    REQUIRE(U(GdiSetBatchLimit));
    U(GdiSetBatchLimit)(6);
    static const DWORD bad[] = { 21, 22, 100, 0x7fffffff, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        /* GdiSetBatchLimit only accepts (dw - 1) < 0x14; otherwise it returns 0 and leaves the limit alone. */
        EXPECT_EQ(U(GdiSetBatchLimit)(bad[i]), 0);
        EXPECT_EQ(U(GdiGetBatchLimit)(), 6);
    }
    lim_restore();
}

TEST(gdi_dcattr_batching_limit_every_value_roundtrips) {
    lim_save();
    REQUIRE(U(GdiSetBatchLimit));
    DWORD prev = U(GdiGetBatchLimit)();
    for (DWORD v = 1; v <= 20; v++) {
        EXPECT_EQ(U(GdiSetBatchLimit)(v), prev);
        EXPECT_EQ(U(GdiGetBatchLimit)(), v);
        prev = v;
    }
    lim_restore();
}

TEST(gdi_dcattr_batching_set_limit_flushes_pending_batch) {
    FXT(f, 8, 8);
    lim_save();
    bo(&f); bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    /* An accepted limit change runs NtGdiFlush first. A rejected one (21) returns before it. */
    EXPECT_EQ(U(GdiSetBatchLimit)(21), 0);
    EXPECT_EQ(teb_count(), 2);
    U(GdiSetBatchLimit)(20);
    FLUSHED();
    lim_restore();
    fx_close(&f);
}

TEST(gdi_dcattr_batching_default_limit_flushes_on_twentieth_call) {
    FXT(f, 8, 8);
    lim_save();
    U(GdiSetBatchLimit)(20);
    bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    for (int i = 2; i <= 19; i++) { bo(&f); EXPECT_EQ(teb_count(), (unsigned)i); }
    EXPECT_EQ(teb_offset(), 19 * 0x10);
    bo(&f);                                   /* count reaches the limit -> NtGdiFlush */
    FLUSHED();
    lim_restore();
    fx_close(&f);
}

TEST(gdi_dcattr_batching_small_limits_flush_when_reached) {
    FXT(f, 8, 8);
    lim_save();
    static const DWORD lims[] = { 2, 3, 5, 8 };
    for (unsigned li = 0; li < sizeof lims / sizeof *lims; li++) {
        U(GdiSetBatchLimit)(lims[li]);
        bo(&f);
        if (teb_count() == 0) { t_note("batch not observed"); break; }
        for (DWORD i = 2; i < lims[li]; i++) { bo(&f); EXPECT_EQ(teb_count(), i); }
        EXPECT_EQ(teb_count(), lims[li] - 1);
        bo(&f);
        FLUSHED();
    }
    lim_restore();
    fx_close(&f);
}

TEST(gdi_dcattr_batching_limit_one_defeats_batching) {
    FXT(f, 8, 8);
    lim_save();
    U(GdiSetBatchLimit)(1);
    for (int i = 0; i < 6; i++) { CHECK(bo(&f)); FLUSHED(); }
    lim_restore();
    fx_close(&f);
}

/* ---- switching and destroying DCs ------------------------------------------------------------ */

TEST(gdi_dcattr_batching_other_dc_call_drains_first_dc_batch) {
    FXT(f1, 8, 8); FXT(f2, 8, 8);
    bo(&f1);
    NEEDBATCH(f1, "SetBrushOrgEx did not queue");
    EXPECT_EQ(teb_hdc(), f1.dc);
    /* The batch holds one DC at a time: a SetBrushOrgEx on a second DC takes the syscall path
     * (NtGdiSetBrushOrg), which drains the first DC's records on entry. */
    CHECK(bo(&f2));
    EXPECT_EQ(teb_count(), 0);
    EXPECT_EQ(teb_offset(), 0);
    if (teb_hdc() != NULL) t_note("TEB.GdiTebBatch.HDC = %p after the switch (dc1 %p, dc2 %p)", teb_hdc(), f1.dc, f2.dc);
    if (f2.a) { POINT p = dca_pt(f2.a, DCA_BRUSHORG); EXPECT_EQ(p.x, g_n); EXPECT_EQ(p.y, g_n + 1); }
    fx_close(&f2); fx_close(&f1);
}

TEST(gdi_dcattr_batching_batch_continues_after_dc_switch) {
    FXT(f1, 8, 8); FXT(f2, 8, 8);
    bo(&f1);
    NEEDBATCH(f1, "SetBrushOrgEx did not queue");
    bo(&f2);                                       /* switch: drained */
    batch_reset();
    bo(&f2);
    if (teb_count() == 0) { t_note("second DC not batched after a switch"); fx_close(&f2); fx_close(&f1); return; }
    EXPECT_EQ(teb_count(), 1);
    EXPECT_EQ(teb_hdc(), f2.dc);
    fx_close(&f2); fx_close(&f1);
}

TEST(gdi_dcattr_batching_deleting_dc_with_pending_batch) {
    FXT(f, 8, 8);
    bo(&f); bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    HDC dc = f.dc;
    U(SelectObject)(dc, f.old); f.old = NULL;
    U(DeleteObject)(f.bmp); f.bmp = NULL;
    CHECK(U(DeleteDC)(dc));
    f.dc = NULL;
    FLUSHED();                                     /* DeleteDC -> NtGdiDeleteObjectApp is a win32k syscall */
    /* The batch refers to a dead handle now; the next batched call on a new DC must still work. */
    FXT(g, 8, 8);
    batch_reset();
    CHECK(bo(&g));
    batch_reset();
    FLUSHED();
    fx_close(&g);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_deleting_brush_may_defer_into_batch) {
    FXT(f, 8, 8);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    REQUIRE(b);
    batch_reset();
    CHECK(U(DeleteObject)(b));
    /* InternalDeleteObject queues a type-7 "delete object" record (header 0x70010) for a brush on a GUI
     * thread whose BRUSHATTR is clean, instead of calling NtGdiDeleteObjectApp. Otherwise it is a syscall. */
    if (teb_count() == 1) {
        EXPECT_EQ(rec_len(0), 0x10);
        EXPECT_EQ(rec_type(0), REC_DELETE7);
        EXPECT_EQ(teb_offset(), 0x10);
        EXPECT_EQ((void *)rdp(teb_rec(0) + 8), (void *)b);
        EXPECT_EQ(teb_hrc(), 0);
    } else {
        t_note("DeleteObject(brush) was a direct syscall (count %u)", teb_count());
    }
    U(GdiFlush)();
    FLUSHED();
    /* Either way the object is gone once the batch has run. */
    EXPECT_EQ(U(GetObjectType)(b), 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_deleting_bitmap_is_a_direct_syscall) {
    FXT(f, 8, 8);
    HBITMAP bm = U(CreateBitmap)(2, 2, 1, 32, NULL);
    REQUIRE(bm);
    bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    CHECK(U(DeleteObject)(bm));
    /* Only brushes/pens/fonts/regions have deferred-delete records; a bitmap goes to NtGdiDeleteObjectApp. */
    EXPECT_EQ(teb_count(), 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_creating_gdi_object_drains_batch) {
    FXT(f, 8, 8);
    bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    HBRUSH b = U(CreateSolidBrush)(RGB(9, 9, 9));
    REQUIRE(b);
    EXPECT_EQ(teb_count(), 0);                     /* NtGdiCreateSolidBrush is a win32k service */
    U(DeleteObject)(b);
    fx_close(&f);
}

/* ---- per-thread state -------------------------------------------------------------------------- */

struct thr { HDC dc; unsigned count, offset; void *teb; HDC hdc; };

static DWORD thread_batch(void *arg) {
    struct thr *t = (struct thr *)arg;
    batch_reset();
    g_n += 2;
    U(SetBrushOrgEx)(t->dc, g_n, g_n + 1, NULL);
    t->count = teb_count(); t->offset = teb_offset(); t->teb = k_teb(); t->hdc = teb_hdc();
    return 0;           /* exits with the batch still queued */
}

TEST(gdi_dcattr_batching_batch_is_per_thread) {
    FXT(f, 8, 8);
    struct thr t; memset(&t, 0, sizeof t); t.dc = f.dc;
    k_run_gui_thread(thread_batch, &t);
    if (t.count == 0) { t_note("worker thread did not queue"); fx_close(&f); return; }
    EXPECT_EQ(t.count, 1);
    EXPECT_EQ(t.offset, 0x10);
    EXPECT_EQ(t.hdc, f.dc);
    CHECK(t.teb != k_teb());
    /* This thread's TEB batch is a different, untouched structure. */
    FLUSHED();
    EXPECT_EQ(teb_hdc(), NULL);
    /* The DC created here stays usable after the worker thread exited with a queued record. */
    CHECK(bo(&f));
    batch_reset();
    if (f.a) { POINT p = dca_pt(f.a, DCA_BRUSHORG); EXPECT_EQ(p.x, g_n); }
    fx_close(&f);
}

/* ---- text, PatBlt and font records ---------------------------------------------------------------- */

TEST(gdi_dcattr_batching_textout_queues_text_record) {
    FXT(f, 64, 32); NEEDA(f);
    CHECK(U(TextOutW)(f.dc, 3, 4, L"A", 1));
    NEEDBATCH(f, "TextOutW did not batch (language pack / emulator)");
    EXPECT_EQ(teb_count(), 1);
    EXPECT_EQ(rec_type(0), REC_TEXTOUT);
    EXPECT_EQ(rec_len(0), text_rec_len(1));
    EXPECT_EQ(teb_offset(), text_rec_len(1));
    EXPECT_EQ(teb_hrc(), 1);                         /* type < 4 sets HasRenderingCommand */
    EXPECT_EQ(teb_hdc(), f.dc);
    EXPECT_EQ(rd32(teb_rec(0) + 0x18), 3);          /* x */
    EXPECT_EQ(rd32(teb_rec(0) + 0x1c), 4);          /* y */
    EXPECT_EQ(rd32(teb_rec(0) + 0x20), 0x80000000);   /* fuOptions | 0x80000000 when there is no rectangle */
    EXPECT_EQ(rd32(teb_rec(0) + 0x38), 1);          /* cChars */
    EXPECT_EQ(rd16(teb_rec(0) + 0x54), L'A');         /* string copied behind the fixed part */
    EXPECT_EQ(dca_u32(f.a, DCA_DIRTY) & 0x600000, 0x600000);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_textout_record_length_formula) {
    FXT(f, 256, 32);
    static const unsigned n[] = { 1, 2, 3, 4, 5, 8, 15, 16, 31, 64, 100 };
    for (unsigned i = 0; i < sizeof n / sizeof *n; i++) {
        WCHAR s[128]; for (unsigned k = 0; k < n[i]; k++) s[k] = L'a' + (k % 26);
        batch_reset();
        CHECK(U(TextOutW)(f.dc, 0, 0, s, n[i]));
        if (teb_count() == 0) { t_note("TextOutW not batched"); break; }
        /* bBatchTextOut: Length = (((2n + 7) & ~7) + 0x58 + 7) & 0xfff8 */
        EXPECT_EQ(rec_len(0), text_rec_len(n[i]));
        EXPECT_EQ(teb_offset(), text_rec_len(n[i]));
        EXPECT_EQ(rd32(teb_rec(0) + 0x38), n[i]);
        EXPECT_EQ(rd16(teb_rec(0) + 0x54 + 2 * (n[i] - 1)), s[n[i] - 1]);
    }
    batch_reset();
    fx_close(&f);
}

TEST(gdi_dcattr_batching_textout_records_accumulate_in_order) {
    FXT(f, 64, 32);
    CHECK(U(TextOutW)(f.dc, 1, 1, L"ab", 2));
    NEEDBATCH(f, "TextOutW did not batch");
    CHECK(U(TextOutW)(f.dc, 9, 8, L"xyz", 3));
    EXPECT_EQ(teb_count(), 2);
    unsigned l0 = text_rec_len(2), l1 = text_rec_len(3);
    EXPECT_EQ(teb_offset(), l0 + l1);
    EXPECT_EQ(rec_len(0), l0); EXPECT_EQ(rec_len(l0), l1);
    EXPECT_EQ(rd32(teb_rec(l0) + 0x18), 9);
    EXPECT_EQ(rd16(teb_rec(l0) + 0x54), L'x');
    fx_close(&f);
}

TEST(gdi_dcattr_batching_textout_buffer_full_falls_back_to_syscall) {
    FXT(f, 256, 32);
    WCHAR s[100]; for (int k = 0; k < 100; k++) s[k] = L'm';
    batch_reset();
    unsigned model = 0, queued = 0, drained = 0;
    lim_save();
    U(GdiSetBatchLimit)(20);
    CHECK(U(TextOutW)(f.dc, 0, 0, s, 100));
    if (teb_count() == 0) { t_note("TextOutW not batched"); lim_restore(); batch_reset(); fx_close(&f); return; }
    model = text_rec_len(100); queued = 1;
    for (int i = 2; i <= 10; i++) {
        CHECK(U(TextOutW)(f.dc, 0, i, s, 100));
        unsigned l = text_rec_len(100);
        if (model + l < TB_FULL) { model += l; queued++; EXPECT_EQ(teb_count(), queued); EXPECT_EQ(teb_offset(), model); }
        else {
            /* The record does not fit (Offset + len must stay < 0x4d9): bBatchTextOut refuses, NtGdiExtTextOutW
             * runs, and the kernel drains the older records on entry. */
            EXPECT_EQ(teb_count(), 0); EXPECT_EQ(teb_offset(), 0);
            model = 0; queued = 0; drained++;
            break;
        }
    }
    CHECKF(drained == 1, "buffer never overflowed (model offset 0x%x)", model);
    lim_restore();
    batch_reset();
    fx_close(&f);
}

TEST(gdi_dcattr_batching_textout_with_updatecp_is_not_batched) {
    FXT(f, 64, 32);
    CHECK(U(TextOutW)(f.dc, 0, 0, L"q", 1));
    NEEDBATCH(f, "TextOutW did not batch");
    batch_reset();
    U(SetTextAlign)(f.dc, TA_UPDATECP);
    CHECK(U(TextOutW)(f.dc, 0, 0, L"q", 1));
    /* bBatchTextOut requires (DC_ATTR.lTextAlign & TA_UPDATECP) == 0. */
    EXPECT_EQ(teb_count(), 0);
    U(SetTextAlign)(f.dc, TA_LEFT);
    batch_reset();
    fx_close(&f);
}

TEST(gdi_dcattr_batching_textout_empty_and_null_strings) {
    NOTWINE("Wine faults on a NULL string where gdi32full validates it");
    FXT(f, 64, 32);
    SetLastError(0);
    /* TextOutW: c == 0 succeeds without doing anything; c < 0 or NULL string fails with 0x57. */
    CHECK(U(TextOutW)(f.dc, 0, 0, L"x", 0));
    EXPECT_EQ(teb_count(), 0);
    SetLastError(0);
    CHECK(!U(TextOutW)(f.dc, 0, 0, NULL, 3));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    CHECK(!U(TextOutW)(f.dc, 0, 0, L"x", -1));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(teb_count(), 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_opaque_rect_queues_rect_record) {
    FXT(f, 64, 32); NEEDA(f);
    RECT rc = { 5, 6, 20, 21 };
    CHECK(U(ExtTextOutW)(f.dc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL));
    NEEDBATCH(f, "ExtTextOutW rect fill did not batch");
    EXPECT_EQ(teb_count(), 1);
    EXPECT_EQ(rec_len(0), 0x28);
    EXPECT_EQ(rec_type(0), REC_TEXTRECT);
    EXPECT_EQ(teb_offset(), 0x28);
    EXPECT_EQ(rd32(teb_rec(0) + 4), dca_u32(f.a, DCA_BKCLR_XL));   /* background colour captured */
    EXPECT_EQ(rd32(teb_rec(0) + 8), ETO_OPAQUE);
    EXPECT_EQ(rd32(teb_rec(0) + 0xc), 5);
    EXPECT_EQ(rd32(teb_rec(0) + 0x10), 6);
    EXPECT_EQ(rd32(teb_rec(0) + 0x14), 20);
    EXPECT_EQ(rd32(teb_rec(0) + 0x18), 21);
    EXPECT_EQ(teb_hrc(), 1);
    CHECK((dca_u32(f.a, DCA_DIRTY) & DIRTY_BATCH_PEND) != 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_patblt_queues_patblt_record_when_not_composited) {
    FXT(f, 64, 32); NEEDA(f);
    CHECK(U(PatBlt)(f.dc, 2, 3, 10, 11, PATCOPY));
    if (teb_count() == 0) { t_note("PatBlt went straight to NtGdiPatBlt (IsThreadDesktopComposited)"); fx_close(&f); return; }
    EXPECT_EQ(teb_count(), 1);
    EXPECT_EQ(rec_len(0), 0x48);
    EXPECT_EQ(rec_type(0), REC_PATBLT);
    EXPECT_EQ(teb_offset(), 0x48);
    EXPECT_EQ(rd32(teb_rec(0) + 4), 2);
    EXPECT_EQ(rd32(teb_rec(0) + 8), 3);
    EXPECT_EQ(rd32(teb_rec(0) + 0xc), 10);
    EXPECT_EQ(rd32(teb_rec(0) + 0x10), 11);
    EXPECT_EQ(rd32(teb_rec(0) + 0x20), PATCOPY);
    CHECK((dca_u32(f.a, DCA_DIRTY) & DIRTY_BATCH_PEND) != 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_selecting_font_queues_font_record) {
    FXT(f, 8, 8);
    HGDIOBJ fa = U(GetStockObject)(SYSTEM_FIXED_FONT), fb = U(GetStockObject)(ANSI_VAR_FONT);
    REQUIRE(fa); REQUIRE(fb);
    HGDIOBJ of = U(SelectObject)(f.dc, fa);
    batch_reset();
    HGDIOBJ r = U(SelectObject)(f.dc, fb);
    EXPECT_EQ(r, fa);
    NEEDBATCH(f, "font selection did not batch");
    EXPECT_EQ(teb_count(), 1);
    EXPECT_EQ(rec_len(0), 0x10);
    EXPECT_EQ(rec_type(0), REC_SELECTFONT);
    EXPECT_EQ((void *)(ULONG_PTR)rdp(teb_rec(0) + 8), (void *)fb);
    EXPECT_EQ(teb_hrc(), 0);
    /* The kernel resolves the queued selection when asked. */
    EXPECT_EQ(U(GetCurrentObject)(f.dc, OBJ_FONT), fb);
    U(SelectObject)(f.dc, of);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_selecting_same_font_again_is_a_noop) {
    FXT(f, 8, 8);
    HGDIOBJ fa = U(GetStockObject)(SYSTEM_FIXED_FONT);
    U(SelectObject)(f.dc, fa);
    batch_reset();
    EXPECT_EQ(U(SelectObject)(f.dc, fa), fa);        /* page already holds it: returned before queueing */
    EXPECT_EQ(teb_count(), 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_mixed_records_walk_consistently) {
    FXT(f, 64, 32);
    bo(&f);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    HGDIOBJ fa = U(GetStockObject)(SYSTEM_FIXED_FONT);
    U(SelectObject)(f.dc, fa);
    U(TextOutW)(f.dc, 0, 0, L"hello", 5);
    RECT rc = { 1, 1, 9, 9 };
    U(ExtTextOutW)(f.dc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
    U(PatBlt)(f.dc, 0, 0, 4, 4, PATCOPY);
    bo(&f);
    /* The kernel walks records with off += (len + 7) & ~7 for GdiBatchCount iterations; the walk must end
     * exactly at Offset, and every header must be inside the buffer. */
    unsigned off = 0, n = 0, count = teb_count(), total = teb_offset();
    while (n < count && off < TB_FULL) {
        unsigned len = rec_len(off), type = rec_type(off);
        CHECKF(len >= 8 && len <= 0x4d8 && type <= 8, "bad header at +0x%x: len 0x%x type %u", off, len, type);
        off += (len + 7) & ~7u; n++;
    }
    EXPECT_EQ(n, count);
    EXPECT_EQ(off, total);
    EXPECT_EQ(teb_hdc(), f.dc);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_setpolyfillmode_flushes_pending_patblt_of_same_dc) {
    FXT(f, 64, 32); NEEDA(f);
    CHECK(U(PatBlt)(f.dc, 0, 0, 4, 4, PATCOPY));
    if (teb_count() == 0 || !(dca_u32(f.a, DCA_DIRTY) & DIRTY_BATCH_PEND)) { t_note("no PatBlt record pending"); fx_close(&f); return; }
    U(SetPolyFillMode)(f.dc, WINDING);
    /* SetPolyFillModeImpl: TEB.HDC == hdc && DC_ATTR pending bit -> NtGdiFlush, then clears the bit itself. */
    FLUSHED();
    EXPECT_EQ(dca_u32(f.a, DCA_DIRTY) & DIRTY_BATCH_PEND, 0);
    U(SetPolyFillMode)(f.dc, ALTERNATE);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_setrop2_flushes_pending_batch_of_same_dc) {
    FXT(f, 64, 32); NEEDA(f);
    CHECK(U(PatBlt)(f.dc, 0, 0, 4, 4, PATCOPY));
    if (teb_count() == 0 || !(dca_u32(f.a, DCA_DIRTY) & DIRTY_BATCH_PEND)) { t_note("no PatBlt record pending"); fx_close(&f); return; }
    U(SetROP2)(f.dc, R2_NOT);
    EXPECT_EQ(teb_count(), 0);
    EXPECT_EQ(dca_u8(f.a, DCA_ROP2), R2_NOT);
    U(SetROP2)(f.dc, R2_COPYPEN);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_char_extra_flushes_pending_text) {
    FXT(f, 64, 32); NEEDA(f);
    CHECK(U(TextOutW)(f.dc, 0, 0, L"t", 1));
    NEEDBATCH(f, "TextOutW did not batch");
    CHECK((dca_u32(f.a, DCA_DIRTY) & DIRTY_BATCH_TEXT) != 0);
    /* SetTextCharacterExtra: TEB.HDC == hdc && pending-text bit -> NtGdiFlush and clears bits 0x600000. */
    U(SetTextCharacterExtra)(f.dc, 2);
    EXPECT_EQ(teb_count(), 0);
    EXPECT_EQ(dca_u32(f.a, DCA_DIRTY) & 0x600000, 0);
    U(SetTextCharacterExtra)(f.dc, 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_window_org_change_flushes_pending_batch) {
    FXT(f, 64, 32); NEEDA(f);
    CHECK(U(PatBlt)(f.dc, 0, 0, 4, 4, PATCOPY));
    if (teb_count() == 0 || !(dca_u32(f.a, DCA_DIRTY) & DIRTY_BATCH_PEND)) { t_note("no PatBlt record pending"); fx_close(&f); return; }
    /* A changed window origin must not be applied under queued drawing that assumed the old one. */
    CHECK(U(SetWindowOrgEx)(f.dc, 5, 5, NULL));
    EXPECT_EQ(teb_count(), 0);
    POINT p = dca_pt(f.a, DCA_WNDORG);
    EXPECT_EQ(p.x, 5); EXPECT_EQ(p.y, 5);
    U(SetWindowOrgEx)(f.dc, 0, 0, NULL);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_font_selection_on_other_dc_drains_first) {
    FXT(f1, 8, 8); FXT(f2, 8, 8);
    bo(&f1);
    NEEDBATCH(f1, "SetBrushOrgEx did not queue");
    HGDIOBJ fa = U(GetStockObject)(SYSTEM_FIXED_FONT);
    U(SelectObject)(f2.dc, fa);          /* TEB.HDC is f1: SelectObjectImpl falls back to NtGdiSelectFont */
    EXPECT_EQ(teb_count(), 0);
    EXPECT_EQ(U(GetCurrentObject)(f2.dc, OBJ_FONT), fa);
    fx_close(&f2); fx_close(&f1);
}
