/* PEB members that the Win32 subsystem owns: KernelCallbackTable / UserSharedInfoPtr (user32), the
 * GdiSharedHandleTable view, GdiDCAttributeList and GdiHandleBuffer (win32kbase!GdiProcessCallout +
 * gdi32full), and the window-station / desktop objects the process is bound to.
 *
 * Anchors: user32!UserClientDllInitialize stores &apfnDispatch into PEB+0x58; win32kbase!GdiProcessCallout
 * stores 0x14 to PEB+0x108, zeroes PEB+0x140..0x230 and maps gpHmgrSharedHandleSection at PEB+0xF8; GDI
 * cells are 0x18 bytes: +0 kernel object, +8 owner pid (bit 0 clear), +0xc handle upper word, +0xe type,
 * +0x10 user-mode attribute page. */
#include "rtp.h"

static ULONG_PTR peb(void) { return (ULONG_PTR)k_peb(); }
static unsigned long long kcb(void) { return rt_rd64(peb(), PEB_KernelCallbackTable); }
static unsigned long long gtab(void) { return rt_rd64(peb(), PEB_GdiSharedHandleTable); }

/* ---- KernelCallbackTable / UserSharedInfoPtr --------------------------------------------------------- */

TEST(r2_teb_peb_field_matrix_peb_kcb_set_after_gui_and_in_user32) {
    rt_gui();
    unsigned long long t = kcb();
    CHECK(t != 0);
    CHECKF(rt_in_user32(t, 8), "KernelCallbackTable=0x%llx", t);
}
TEST(r2_teb_peb_field_matrix_peb_kcb_pointer_aligned) {
    rt_gui();
    EXPECT_EQ(kcb() & 7, 0);
}
TEST(r2_teb_peb_field_matrix_peb_kcb_entries_point_into_user32) {
    rt_gui();
    unsigned n = k_cb_count();
    t_note("kernel callback table has %u consecutive non-NULL entries", n);
    CHECK(n >= 50);                                        /* apfnDispatch is a dense table of ~129 user32 callbacks on 19041; floor kept loose */
    unsigned outside = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned long long p = rt_rd64((ULONG_PTR)kcb(), i * 8);
        if (!rt_in_user32(p, 1)) { outside++; t_note("entry %u = 0x%llx outside user32", i, p); }
    }
    EXPECT_EQ(outside, 0);                                 /* every slot is a user32 function (verified by RVA resolution) */
}
TEST(r2_teb_peb_field_matrix_peb_kcb_user_shared_info_alias) {
    rt_gui();
    EXPECT_EQ(PEB_UserSharedInfoPtr, PEB_KernelCallbackTable);   /* anonymous union in _PEB */
    EXPECT_EQ(rt_rd64(peb(), PEB_UserSharedInfoPtr), kcb());
}
TEST(r2_teb_peb_field_matrix_peb_kcb_stable_across_calls_and_threads) {
    rt_gui();
    unsigned long long a = kcb();
    WK_GUI(w);
    EXPECT_EQ(kcb(), a);                                   /* one table per process */
    wk_finish(&w);
    t_proc("GetFocus");
    EXPECT_EQ(kcb(), a);
}
TEST(r2_teb_peb_field_matrix_peb_kcb_matches_k_cb_table_helper) {
    rt_gui();
    EXPECT_EQ((unsigned long long)(ULONG_PTR)k_cb_table(), kcb());
}
TEST(r2_teb_peb_field_matrix_peb_kcb_page_readable_and_in_image_data) {
    rt_gui();
    MEMORY_BASIC_INFORMATION mbi; memset(&mbi, 0, sizeof mbi);
    unsigned long long t = kcb();
    REQUIRE(t != 0);
    CHECK(VirtualQuery((void *)(ULONG_PTR)t, &mbi, sizeof mbi) != 0);
    EXPECT_EQ(mbi.State, MEM_COMMIT);
    EXPECT_EQ(mbi.Type, MEM_IMAGE);                        /* apfnDispatch is a user32 data symbol */
    t_note("kcb page protect=0x%lx", mbi.Protect);
}
TEST(r2_teb_peb_field_matrix_peb_kcb_null_in_pristine_child_and_set_after) {
    struct k_child_result r = k_child_run("r2_teb_peb_field_matrix_c_user32_first", "", 30000);
    if (r.exit_code == 0xDEAD0000) { t_skip("child did not start"); return; }
    EXPECT_EQ(r.exit_code, 0);
}

/* ---- GdiSharedHandleTable / GdiDCAttributeList / GdiHandleBuffer ---------------------------------------- */

TEST(r2_teb_peb_field_matrix_peb_gdi_table_mapped_after_gui) {
    rt_gui();
    unsigned long long t = gtab();
    REQUIRE(t != 0);
    CHECK(rt_user_va(t));
    EXPECT_EQ(t & 0xFFF, 0);
    MEMORY_BASIC_INFORMATION mbi; memset(&mbi, 0, sizeof mbi);
    CHECK(VirtualQuery((void *)(ULONG_PTR)t, &mbi, sizeof mbi) != 0);
    EXPECT_EQ(mbi.Type, MEM_MAPPED);
    EXPECT_EQ(mbi.State, MEM_COMMIT);
}
TEST(r2_teb_peb_field_matrix_peb_gdi_table_view_is_accessible) {
    rt_gui();
    unsigned long long t = gtab();
    REQUIRE(t != 0);
    MEMORY_BASIC_INFORMATION mbi; memset(&mbi, 0, sizeof mbi);
    CHECK(VirtualQuery((void *)(ULONG_PTR)t, &mbi, sizeof mbi) != 0);
    t_note("GdiSharedHandleTable protect=0x%lx", mbi.Protect);
    CHECK((mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY)) != 0);
}
TEST(r2_teb_peb_field_matrix_peb_gdi_table_same_for_every_thread) {
    rt_gui();
    unsigned long long t = gtab();
    WK_GUI(w);
    EXPECT_EQ(gtab(), t);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_peb_gdi_table_stable_across_object_creation) {
    rt_gui();
    unsigned long long t = gtab();
    HDC d = U(CreateCompatibleDC)(NULL);
    HBITMAP b = U(CreateBitmap)(4, 4, 1, 32, NULL);
    EXPECT_EQ(gtab(), t);
    if (b) U(DeleteObject)(b);
    if (d) U(DeleteDC)(d);
}
TEST(r2_teb_peb_field_matrix_peb_gdi_dcattr_list_is_0x14) {
    rt_gui();
    EXPECT_EQ(rt_rd32(peb(), PEB_GdiDCAttributeList), K_GDI_DC_ATTRIBUTE_LIST_INIT);
}
TEST(r2_teb_peb_field_matrix_peb_gdi_dcattr_list_unchanged_by_dc_traffic) {
    rt_gui();
    unsigned long v = rt_rd32(peb(), PEB_GdiDCAttributeList);
    for (int i = 0; i < 8; i++) { HDC d = U(CreateCompatibleDC)(NULL); if (d) U(DeleteDC)(d); }
    EXPECT_EQ(rt_rd32(peb(), PEB_GdiDCAttributeList), v);
}
TEST(r2_teb_peb_field_matrix_peb_gdi_handle_buffer_region_readable) {
    rt_gui();
    unsigned char b[0xF0];
    CHECK(k_try_read((char *)k_peb() + PEB_GdiHandleBuffer, b, sizeof b));
    unsigned nz = 0; for (unsigned i = 0; i < sizeof b; i++) if (b[i]) nz++;
    t_note("GdiHandleBuffer non-zero bytes: %u of %u", nz, (unsigned)sizeof b);
}
TEST(r2_teb_peb_field_matrix_peb_gdi_handle_buffer_size_matches_pdb) {
    EXPECT_EQ(PEB_GdiHandleBuffer + 60 * 4, 0x230);        /* GdiHandleBuffer[60] ends at +0x230 (PostProcessInitRoutine follows) */
}

/* ---- cells of objects created on other threads ---------------------------------------------------------- */

static void op_make_dc(struct wk *w) {
    HDC d = U(CreateCompatibleDC)(NULL);
    w->x[0] = (unsigned long long)(ULONG_PTR)d;
    struct k_gdicell c; memset(&c, 0, sizeof c);
    if (d && k_gdi_cell(d, &c)) {
        w->x[1] = 1;
        w->x[2] = c.field8;
        w->x[3] = (unsigned long long)(ULONG_PTR)c.field10;
        w->x[4] = c.type_check;
    }
    /* the object outlives the thread's win32k state only if we leave it; delete here */
    if (d) U(DeleteDC)(d);
}
TEST(r2_teb_peb_field_matrix_peb_gdi_cell_of_worker_dc_names_this_process) {
    rt_gui();
    struct wk w;
    if (!wk_start(&w, 1, op_make_dc, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    REQUIRE(w.x[0] != 0);
    REQUIRE(w.x[1] != 0);
    EXPECT_EQ((unsigned long)w.x[2] & ~1UL, GetCurrentProcessId() & ~3UL);   /* cell owner word = pid with low bits masked */
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_peb_gdi_cell_user_page_is_user_pointer) {
    rt_gui();
    struct wk w;
    if (!wk_start(&w, 1, op_make_dc, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    REQUIRE(w.x[1] != 0);
    CHECKF(rt_user_va(w.x[3]), "GDICELL.pUser=0x%llx", w.x[3]);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_peb_gdi_handle_index_fits_table_and_upper_word_matches) {
    rt_gui();
    HDC d = U(CreateCompatibleDC)(NULL);
    REQUIRE(d);
    ULONG_PTR h = (ULONG_PTR)d;
    unsigned long long t = gtab();
    REQUIRE(t != 0);
    unsigned short upper = 0;
    CHECK(k_try_read_u16((void *)(ULONG_PTR)(t + (h & 0xFFFF) * GDI_CELL_SIZE + 0xC), &upper));
    EXPECT_EQ(upper, (unsigned short)((h >> 16) & 0xFFFF));        /* GdiGetEntry compares HIWORD(handle) with cell+0xc */
    U(DeleteDC)(d);
}

/* ---- window station / desktop the process is bound to ---------------------------------------------------- */

UDECL(LONG, NtQueryObject, (HANDLE, int, PVOID, ULONG, PULONG));
static int type_name_of(HANDLE h, wchar_t *out, size_t cap) {
    NtQueryObject_pfn q = (NtQueryObject_pfn)rt_ntdll("NtQueryObject");
    if (!q) return 0;
    unsigned char buf[512]; ULONG len = 0;
    if (q(h, 2, buf, sizeof buf, &len) < 0) return 0;               /* ObjectTypeInformation */
    UNICODE_STRING *us = (UNICODE_STRING *)buf;
    size_t n = us->Length / 2; if (n >= cap) n = cap - 1;
    memcpy(out, us->Buffer, n * 2); out[n] = 0;
    return 1;
}
TEST(r2_teb_peb_field_matrix_proc_winsta_handle_is_window_station_object) {
    rt_gui();
    REQUIRE(U(GetProcessWindowStation));
    HWINSTA ws = U(GetProcessWindowStation)();
    REQUIRE(ws != NULL);
    wchar_t tn[64];
    CHECK(type_name_of((HANDLE)ws, tn, 64));
    EXPECT_WSTR(tn, L"WindowStation");
}
TEST(r2_teb_peb_field_matrix_proc_winsta_stable_across_threads) {
    rt_gui();
    REQUIRE(U(GetProcessWindowStation));
    HWINSTA a = U(GetProcessWindowStation)();
    WK_GUI(w);
    EXPECT_EQ(U(GetProcessWindowStation)(), a);                      /* process-wide */
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_proc_thread_desktop_is_desktop_object) {
    rt_gui();
    REQUIRE(U(GetThreadDesktop));
    HDESK d = U(GetThreadDesktop)(GetCurrentThreadId());
    REQUIRE(d != NULL);
    wchar_t tn[64];
    CHECK(type_name_of((HANDLE)d, tn, 64));
    EXPECT_WSTR(tn, L"Desktop");
}
TEST(r2_teb_peb_field_matrix_proc_thread_desktop_same_for_main_and_worker) {
    rt_gui();
    REQUIRE(U(GetThreadDesktop));
    HDESK a = U(GetThreadDesktop)(GetCurrentThreadId());
    WK_GUI(w);
    HDESK b = U(GetThreadDesktop)(w.tid);
    CHECK(a != NULL && b != NULL);
    EXPECT_EQ(a, b);                                                 /* a new thread joins the process's initial desktop */
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_proc_thread_desktop_stub_matches_wrapper) {
    rt_gui();
    NtUserGetThreadDesktop_pfn f = (NtUserGetThreadDesktop_pfn)t_proc_in("win32u.dll", "NtUserGetThreadDesktop");
    REQUIRE(f); REQUIRE(U(GetThreadDesktop));
    EXPECT_EQ(f(GetCurrentThreadId()), U(GetThreadDesktop)(GetCurrentThreadId()));
}
TEST(r2_teb_peb_field_matrix_proc_thread_desktop_of_bad_tid_is_null) {
    rt_gui();
    NtUserGetThreadDesktop_pfn f = (NtUserGetThreadDesktop_pfn)t_proc_in("win32u.dll", "NtUserGetThreadDesktop");
    REQUIRE(f);
    SetLastError(0);
    HANDLE h = f(0xFFFFFFF0);
    EXPECT_EQ(h, NULL);
}
TEST(r2_teb_peb_field_matrix_proc_desktop_name_via_user_object_info) {
    rt_gui();
    REQUIRE(U(GetThreadDesktop)); REQUIRE(U(GetUserObjectInformationW));
    HDESK d = U(GetThreadDesktop)(GetCurrentThreadId());
    REQUIRE(d);
    wchar_t nm[64] = {0}; DWORD need = 0;
    CHECK(U(GetUserObjectInformationW)(d, UOI_NAME, nm, sizeof nm, &need));
    CHECK(nm[0] != 0);
    t_note("desktop name %ls", nm);
}
TEST(r2_teb_peb_field_matrix_proc_gui_resources_count_gdi_dc) {
    rt_gui();
    REQUIRE(U(GetGuiResources));
    DWORD a = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    HDC d = U(CreateCompatibleDC)(NULL);
    REQUIRE(d);
    DWORD b = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    EXPECT_EQ(b, a + 1);
    U(DeleteDC)(d);
    EXPECT_EQ(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS), a);
}
TEST(r2_teb_peb_field_matrix_proc_gui_resources_same_from_worker) {
    rt_gui();
    REQUIRE(U(GetGuiResources));
    DWORD a = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    WK_GUI(w);
    DWORD b = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    t_note("GDI objects before=%lu with live GUI worker=%lu", a, b);
    CHECK(b >= a);
    wk_finish(&w);
}
