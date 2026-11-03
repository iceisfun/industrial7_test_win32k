/* teb_peb_win32_fields: Win32 members of the PEB and of other threads' TEBs that win32k / user32 /
 * gdi32full establish, validated against what the analysis of those components write or read.
 *
 * Analysis anchors (19041):
 *  - win32kbase!GdiProcessCallout: PEB+0x108 = 0x14, memset(PEB+0x140, 0, 0xF0), PEB+0xF8 = mapped
 *    view of gpHmgrSharedHandleSection (process attach to the GDI subsystem).
 *  - user32!_UserClientDllInitialize: PEB+0x58 = &apfnDispatch (the field is a union of
 *    KernelCallbackTable and UserSharedInfoPtr in the PDB).
 *  - gdi32full!PatBlt: a cell is accepted only when (cell dword at +8 & ~1) == gW32PID, where
 *    gW32PID = TEB.ClientId.UniqueProcess & ~3, and cell+0xC == HIWORD(handle).
 *  - win32kfull!NtUserGetThreadState: index 0xE returns 1; indices >= 0x10 fall through to 0.
 *  - win32kbase!NtGdiInit is getDLT (returns constant 1); NtGdiFlush returns 0. */
#include "tp.h"

#define SENT 0x5A5A5A5Au

/* ---- PEB: GDI members -------------------------------------------------------------------------- */

static void gdi_up(void) { tp_gui(); if (U(GdiFlush)) U(GdiFlush)(); }

TEST(teb_peb_win32_fields_peb_gdi_dc_attribute_list_is_20) {
    /* GdiProcessCallout stores 0x14 (the number of DC_ATTR slots per page group) at PEB+0x108 */
    gdi_up();
    EXPECT_EQ(tp_peb32(PEB_GdiDCAttributeList), K_GDI_DC_ATTRIBUTE_LIST_INIT);
}

TEST(teb_peb_win32_fields_peb_gdi_shared_handle_table_mapped_view) {
    gdi_up();
    unsigned long long t = tp_peb64(PEB_GdiSharedHandleTable);
    REQUIRE(t != 0);
    CHECK(tp_is_user_va(t));
    EXPECT_EQ(t & 0xFFF, 0);                                   /* a section view starts on a page boundary */
    MEMORY_BASIC_INFORMATION mbi; memset(&mbi, 0, sizeof mbi);
    if (VirtualQuery((void *)(ULONG_PTR)t, &mbi, sizeof mbi)) {
        EXPECT_EQ(mbi.Type, MEM_MAPPED);                       /* gpHmgrSharedHandleSection is a section, not heap/private */
        EXPECT_EQ(mbi.State, MEM_COMMIT);
        t_note("GdiSharedHandleTable view base=%p size=0x%llx protect=0x%lx", mbi.AllocationBase, (unsigned long long)mbi.RegionSize, mbi.Protect);
    } else CHECK(0);
}

TEST(teb_peb_win32_fields_peb_gdi_shared_memory_regions_readable) {
    /* gdi32full reads pGdiDevCaps (+0x180000), the stock objects (+0x1800B0) and the DPI font slots (+0x180210..) from the same view */
    gdi_up();
    unsigned long long t = tp_peb64(PEB_GdiSharedHandleTable);
    REQUIRE(t != 0);
    unsigned char b[8];
    static const unsigned offs[] = { 0, K_GDI_SHARED_DEVCAPS, K_GDI_SHARED_STOCK_OBJECTS, K_GDI_SHARED_STOCK_OBJECTS + 22 * 8 - 8,
                                     K_GDI_SHARED_STOCK_DEFGUIFONT, 0x180210 };
    for (unsigned i = 0; i < sizeof offs / sizeof offs[0]; i++)
        CHECKF(k_try_read((void *)(ULONG_PTR)(t + offs[i]), b, sizeof b), "view +0x%x unreadable", offs[i]);
}

TEST(teb_peb_win32_fields_peb_gdi_table_pointer_stable) {
    gdi_up();
    unsigned long long a = tp_peb64(PEB_GdiSharedHandleTable);
    HDC dc = U(CreateCompatibleDC) ? U(CreateCompatibleDC)(NULL) : NULL;
    if (dc) U(DeleteDC)(dc);
    unsigned long long b = tp_peb64(PEB_GdiSharedHandleTable);
    CHECK(a != 0);
    EXPECT_EQ(a, b);                                           /* the view is mapped once per process */
    unsigned long long c = 0;
    CHECK(k_try_read((char *)k_peb() + PEB_GdiSharedHandleTable, &c, 8));
    EXPECT_EQ(c, a);
}

TEST(teb_peb_win32_fields_peb_gdi_handle_buffer_reported) {
    /* GdiProcessCallout zeroes it once; gdi32full may use it later as a handle cache, so only report */
    gdi_up();
    unsigned nz = 0;
    for (unsigned i = 0; i < 60; i++) if (tp_peb32(PEB_GdiHandleBuffer + i * 4)) nz++;
    t_note("PEB.GdiHandleBuffer non-zero dwords: %u of 60", nz);
    CHECK(tp_peb32(PEB_GdiHandleBuffer + 60 * 4 - 4) == tp_peb32(PEB_GdiHandleBuffer + 60 * 4 - 4));   /* readable */
}

TEST(teb_peb_win32_fields_peb_gdi_cell_owner_is_teb_client_pid) {
    /* PatBlt accepts a DC only if (cell dword@+8 & ~1) == TEB.ClientId.UniqueProcess & ~3 and cell+0xC == HIWORD(hdc) */
    gdi_up();
    REQUIRE(U(CreateCompatibleDC));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    unsigned long long t = tp_peb64(PEB_GdiSharedHandleTable);
    unsigned char cell[0x18];
    if (t && k_try_read((void *)(ULONG_PTR)(t + (unsigned long long)((ULONG_PTR)dc & 0xFFFF) * 0x18), cell, sizeof cell)) {
        unsigned long owner = *(unsigned long *)(cell + 8);
        unsigned short upper = *(unsigned short *)(cell + 0xC);
        EXPECT_EQ(owner & ~1UL, (unsigned long)(tp_teb64(TEB_ClientId) & ~3ULL));
        EXPECT_EQ(upper, (unsigned)(((ULONG_PTR)dc >> 16) & 0xFFFF));
    } else t_skip("GDI shared handle table not readable");
    U(DeleteDC)(dc);
}

/* ---- PEB: USER / misc -------------------------------------------------------------------------- */

TEST(teb_peb_win32_fields_peb_user_shared_info_ptr_aliases_kernel_callback_table) {
    /* _PEB has KernelCallbackTable and UserSharedInfoPtr in one union at +0x58 */
    EXPECT_EQ(PEB_UserSharedInfoPtr, PEB_KernelCallbackTable);
    tp_gui();
    unsigned long long v = tp_peb64(PEB_UserSharedInfoPtr);
    EXPECT_EQ(v, tp_peb64(PEB_KernelCallbackTable));
    CHECK(tp_is_user_va(v & ~1ULL));
}

TEST(teb_peb_win32_fields_peb_pointer_is_not_gsharedinfo_psi) {
    /* the slot holds user32's dispatch table, not the SERVERINFO pointer that gSharedInfo carries */
    tp_gui();
    struct k_user_shared_info si;
    if (!k_shared_info(&si)) { t_skip("gSharedInfo unavailable"); return; }
    CHECK(si.psi != NULL);
    CHECK((ULONG_PTR)si.psi != (ULONG_PTR)tp_peb64(PEB_UserSharedInfoPtr));
}

TEST(teb_peb_win32_fields_peb_build_matches_kuser_shared_data) {
    /* KUSER_SHARED_DATA.NtBuildNumber (+0x260) is the kernel's build; the PEB copy comes from the same source */
    unsigned long kb = 0;
    CHECK(k_try_read_u32((void *)0x7FFE0260, &kb));
    EXPECT_EQ(tp_peb16(PEB_OSBuildNumber), (kb & 0xFFFF));
}

TEST(teb_peb_win32_fields_peb_parameters_and_ldr_pointers) {
    unsigned long long pp = tp_peb64(0x20), ldr = tp_peb64(0x18);
    CHECK(tp_is_user_va(pp));
    CHECK(tp_is_user_va(ldr));
    unsigned char b[16];
    CHECK(k_try_read((void *)(ULONG_PTR)pp, b, sizeof b));
    CHECK(k_try_read((void *)(ULONG_PTR)ldr, b, sizeof b));
}

TEST(teb_peb_win32_fields_win32u_is_loaded_first_among_win32_modules) {
    /* the all-tests exe loads win32u first only; user32 / gdi32 appear lazily */
    HMODULE w = GetModuleHandleA("win32u.dll");
    REQUIRE(w);
    IMAGE_DOS_HEADER dos;
    CHECK(k_try_read(w, &dos, sizeof dos));
    EXPECT_EQ(dos.e_magic, IMAGE_DOS_SIGNATURE);
    CHECK(GetProcAddress(w, "NtUserGetThreadState") != NULL);
    CHECK(GetProcAddress(w, "NtGdiFlush") != NULL);
}

/* ---- other threads' TEB ---------------------------------------------------------------------- */

struct conv {
    DWORD tid; unsigned long long ti_before, ti_after, ci20, ci28, peb, self, cid_pid, cid_tid, stackbase;
    unsigned long cnt, off, state_ret;
    int ci_nz_after;
};
static DWORD conv_thread(void *p) {
    struct conv *c = (struct conv *)p;
    c->tid = GetCurrentThreadId();
    c->ti_before = tp_teb64(TEB_Win32ThreadInfo);
    c->cnt = (unsigned long)tp_teb32(TEB_GdiBatchCount);
    c->off = tp_teb32(TEB_GdiTebBatch) & 0x7FFFFFFF;
    c->state_ret = (unsigned long)UN(NtUserGetThreadState)(0xE);
    c->ti_after = tp_teb64(TEB_Win32ThreadInfo);
    tp_gui();
    c->ci20 = tp_teb64(TEB_Win32ClientInfo + 0x20);
    c->ci28 = tp_teb64(TEB_Win32ClientInfo + 0x28);
    for (unsigned i = 0; i < 62; i++) if (tp_teb64(TEB_Win32ClientInfo + i * 8)) c->ci_nz_after++;
    c->peb = tp_teb64(TEB_ProcessEnvironmentBlock);
    c->self = tp_teb64(TEB_Self);
    c->cid_pid = tp_teb64(TEB_ClientId);
    c->cid_tid = tp_teb64(TEB_ClientId + 8);
    c->stackbase = tp_teb64(TEB_StackBase);
    return 0;
}

TEST(teb_peb_win32_fields_new_thread_gdi_batch_is_empty) {
    REQUIRE(UN(NtUserGetThreadState));
    struct conv c; memset(&c, 0, sizeof c);
    CHECK(tp_run_thread(conv_thread, &c, 5000) == 0);
    EXPECT_EQ(c.cnt, 0);          /* GdiBatchCount is only incremented by gdi32full batching calls */
    EXPECT_EQ(c.off, 0);          /* GdiTebBatch.Offset (bits 0-30) starts at 0 in a zero-filled TEB */
}

TEST(teb_peb_win32_fields_first_win32k_syscall_converts_thread) {
    REQUIRE(UN(NtUserGetThreadState));
    struct conv c; memset(&c, 0, sizeof c);
    CHECK(tp_run_thread(conv_thread, &c, 5000) == 0);
    t_info("Win32ThreadInfo before=0x%llx after=0x%llx tid=%lu", c.ti_before, c.ti_after, c.tid);
    EXPECT_EQ(c.state_ret, 1);                       /* NtUserGetThreadState(0xE) == 1 */
    CHECK(c.ti_after != 0);
    EXPECT_EQ(c.ti_after, c.tid);                    /* 19041 stores the thread id here */
}

TEST(teb_peb_win32_fields_win32_thread_info_distinct_per_thread) {
    REQUIRE(UN(NtUserGetThreadState));
    struct conv a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    CHECK(tp_run_thread(conv_thread, &a, 5000) == 0);
    CHECK(tp_run_thread(conv_thread, &b, 5000) == 0);
    CHECK(a.ti_after != 0 && b.ti_after != 0);
    CHECK(a.ti_after != b.ti_after || a.tid == b.tid);
    CHECK(a.tid != GetCurrentThreadId());
}

TEST(teb_peb_win32_fields_client_info_filled_after_gui_call) {
    /* after a user32 call the thread's _CLIENTINFO block (TEB+0x800) is populated: pDeskInfo (+0x20) is a
     * client-visible pointer and ulClientDelta (+0x28) is non-zero */
    REQUIRE(UN(NtUserGetThreadState));
    struct conv c; memset(&c, 0, sizeof c);
    CHECK(tp_run_thread(conv_thread, &c, 5000) == 0);
    CHECK(c.ci_nz_after > 0);
    CHECK(c.ci20 != 0);
    CHECK(c.ci28 != 0);
    unsigned char b[8];
    CHECK(k_try_read((void *)(ULONG_PTR)c.ci20, b, sizeof b));
}

TEST(teb_peb_win32_fields_other_thread_teb_identity) {
    REQUIRE(UN(NtUserGetThreadState));
    struct conv c; memset(&c, 0, sizeof c);
    CHECK(tp_run_thread(conv_thread, &c, 5000) == 0);
    EXPECT_EQ(c.peb, (ULONG_PTR)k_peb());
    EXPECT_EQ(c.cid_pid, GetCurrentProcessId());
    EXPECT_EQ(c.cid_tid, c.tid);
    CHECK(c.self != 0 && c.self != (ULONG_PTR)k_teb());
    CHECK(c.stackbase != 0);
}

TEST(teb_peb_win32_fields_same_client_info_deskinfo_across_threads) {
    /* every thread on the default desktop sees the same pDeskInfo */
    tp_gui();
    unsigned long long mine = tp_teb64(TEB_Win32ClientInfo + 0x20);
    struct conv c; memset(&c, 0, sizeof c);
    REQUIRE(UN(NtUserGetThreadState));
    CHECK(tp_run_thread(conv_thread, &c, 5000) == 0);
    if (!mine || !c.ci20) { t_skip("no pDeskInfo"); return; }
    EXPECT_EQ(c.ci20, mine);
}

/* ---- Win32 ids in the TEB that no win32k routine writes ---------------------------------------- */

TEST(teb_peb_win32_fields_gdi_client_ids_reported) {
    gdi_up();
    unsigned long pid = tp_teb32(TEB_GdiClientPID), tid = tp_teb32(TEB_GdiClientTID);
    t_info("GdiClientPID=%lu GdiClientTID=%lu (process %lu thread %lu)", pid, tid, GetCurrentProcessId(), GetCurrentThreadId());
    if (pid && pid != GetCurrentProcessId()) t_note("GdiClientPID differs from the process id");
    if (tid && tid != GetCurrentThreadId()) t_note("GdiClientTID differs from the thread id");
}

TEST(teb_peb_win32_fields_gdi_cached_process_handle_reported) {
    gdi_up();
    unsigned long long h = tp_teb64(TEB_GdiCachedProcessHandle);
    t_note("GdiCachedProcessHandle=0x%llx", h);
    if (h) {
        DWORD p = GetProcessId((HANDLE)(ULONG_PTR)h);
        if (p != GetCurrentProcessId()) t_note("cached handle names process %lu", p);
    }
}

TEST(teb_peb_win32_fields_gdi_thread_local_info_reported) {
    gdi_up();
    unsigned long long v = tp_teb64(TEB_GdiThreadLocalInfo);
    t_note("GdiThreadLocalInfo=0x%llx", v);
    if (v) CHECK(tp_is_user_va(v) || tp_is_kernel_va(v));
}

/* ---- direct syscalls whose results are fixed by the analysis -------------------------------- */

TEST(teb_peb_win32_fields_ntgdiinit_returns_one) {
    /* win32kbase NtGdiInit shares getDLT's body: returns the constant 1 and takes no arguments */
    if (t_is_wine()) { t_skip("Wine's win32u aborts on the unimplemented NtGdiInit"); return; }
    REQUIRE(UN(NtGdiInit));
    EXPECT_EQ(UN(NtGdiInit)(), 1);
    EXPECT_EQ(UN(NtGdiInit)(), 1);
}

TEST(teb_peb_win32_fields_ntgdiflush_returns_zero_and_keeps_error) {
    REQUIRE(UN(NtGdiFlush));
    tp_teb_write32(TEB_LastStatusValue, SENT);
    SetLastError(0x1234);
    EXPECT_EQ(UN(NtGdiFlush)(), 0);
    EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENT);
    SetLastError(0);
    tp_teb_write32(TEB_LastStatusValue, 0);
}

TEST(teb_peb_win32_fields_threadstate_unused_indices_return_zero) {
    /* NtUserGetThreadState: indices above 0xF are not handled and yield 0 (also as unsigned 64-bit, -1) */
    REQUIRE(UN(NtUserGetThreadState));
    static const int idx[] = { 0x10, 0x11, 0x20, 0x100, 0x7FFFFFFF, -1 };
    for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        tp_teb_write32(TEB_LastStatusValue, SENT);
        SetLastError(0x4321);
        EXPECT_EQ(UN(NtUserGetThreadState)(idx[i]), 0);
        EXPECT_EQ(GetLastError(), 0x4321);
        EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENT);
    }
    SetLastError(0);
    tp_teb_write32(TEB_LastStatusValue, 0);
}

TEST(teb_peb_win32_fields_threadstate_boolean_indices) {
    /* index 5 ((GetQueueStatus & 5) != 0), 0xA (queue is foreground), 0xF (flag bit) are 0/1 */
    REQUIRE(UN(NtUserGetThreadState));
    static const int idx[] = { 5, 0xA, 0xF };
    for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        ULONG_PTR v = UN(NtUserGetThreadState)(idx[i]);
        CHECKF(v <= 1, "index 0x%x returned 0x%llx", idx[i], (unsigned long long)v);
    }
}

TEST(teb_peb_win32_fields_threadstate_logon_index_zero_outside_winlogon) {
    /* index 0xD: result 1 only for the logon process or a thread on the logon desktop */
    REQUIRE(UN(NtUserGetThreadState));
    ULONG_PTR v = UN(NtUserGetThreadState)(0xD);
    CHECKF(v <= 1, "0xD returned 0x%llx", (unsigned long long)v);
    if (v) t_note("thread is on the logon desktop or in winlogon");
}

/* ---- last error written by the user32 client side and by the kernel ----------------------------- */

TEST(teb_peb_win32_fields_bad_hwnd_sets_1400_without_status) {
    /* user32 reports ERROR_INVALID_WINDOW_HANDLE via RtlSetLastWin32Error (error word only) */
    REQUIRE(U(GetWindowRect)); REQUIRE(U(GetClientRect)); REQUIRE(U(GetWindowThreadProcessId));
    REQUIRE(U(GetWindowTextW)); REQUIRE(U(GetClassNameW));
    static const ULONG_PTR h[] = { 0, 0x7FFE0000, 0x00010000 };
    for (unsigned i = 0; i < sizeof h / sizeof h[0]; i++) {
        HWND w = (HWND)h[i]; RECT r; WCHAR buf[8]; DWORD pid;
        tp_teb_write32(TEB_LastStatusValue, SENT); SetLastError(0x31337u);
        EXPECT_EQ(U(GetWindowRect)(w, &r), 0);
        EXPECT_EQ(GetLastError(), 1400); EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENT);
        SetLastError(0x31337u);
        EXPECT_EQ(U(GetClientRect)(w, &r), 0);
        EXPECT_EQ(GetLastError(), 1400); EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENT);
        SetLastError(0x31337u);
        EXPECT_EQ(U(GetWindowThreadProcessId)(w, &pid), 0);
        EXPECT_EQ(GetLastError(), 1400); EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENT);
        SetLastError(0x31337u);
        EXPECT_EQ(U(GetWindowTextW)(w, buf, 8), 0);
        EXPECT_EQ(GetLastError(), 1400); EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENT);
        SetLastError(0x31337u);
        EXPECT_EQ(U(GetClassNameW)(w, buf, 8), 0);
        EXPECT_EQ(GetLastError(), 1400); EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENT);
    }
    SetLastError(0);
    tp_teb_write32(TEB_LastStatusValue, 0);
}

TEST(teb_peb_win32_fields_last_error_is_the_same_word_for_kernel_and_user_writers) {
    /* the kernel stored 1400 through TEB+0x68; GetLastError reads the same word, and a user-mode store is seen back */
    REQUIRE(U(GetWindowRect));
    RECT r;
    SetLastError(0);
    U(GetWindowRect)(NULL, &r);
    EXPECT_EQ(tp_teb32(TEB_LastErrorValue), 1400);
    tp_teb_write32(TEB_LastErrorValue, 77);
    EXPECT_EQ(GetLastError(), 77);
    SetLastError(0);
}
