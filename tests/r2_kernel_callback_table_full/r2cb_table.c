/* r2_kernel_callback_table_full: the table itself, slot by slot (parent process, read-only).
 *
 * Ground truth for every statement here:
 *  - ntdll!KiUserCallbackDispatcher passes the buffer, length and ApiNumber to the routine found by
 *    indexing PEB->KernelCallbackTable with the ApiNumber. No bounds check;
 *    after a normal return it calls NtCallbackReturn(NULL, 0, result).
 *  - user32!apfnDispatch holds 129 code pointers (r2cb_data.h) followed by a NULL qword.
 *  - for every slot with a kernel caller (r2_ref_kfn) the caller's analysed KeUserModeCallback(N, ...)
 *    uses N == slot index, e.g. the menu-load callback -> 0x4c (76), the thread-setup callback -> 0x54 (84),
 *    the APC-delivery callback -> 0x55 (85), the DWORD message thunk -> 2, the create-struct thunk -> 10.
 *  - every distinct user32 thunk except __ClientCallDummyCallback (raises 0xC000001D) contains a call or
 *    jmp through the IAT slot of ntdll!NtCallbackReturn (checked for all 111 on 19041.2965).
 */
#include "r2cb.h"
#include "r2cb_data.h"

static void *slot_at(unsigned i) {
    void *tbl = k_cb_table(), *p = NULL;
    if (!tbl) return NULL;
    k_try_read_ptr((char *)tbl + (size_t)i * sizeof(void *), &p);
    return p;
}
static int table_ready(void) {
    if (!t_proc("IsWindow")) return 0;
    IsWindow((HWND)0);
    return k_cb_table() != NULL;
}
static int in_text(void *p) {
    ULONG_PTR lo, hi;
    return r2_user32_text(&lo, &hi) && (ULONG_PTR)p >= lo && (ULONG_PTR)p < hi;
}

R2T(table_count_at_least_0x7c) {
    if (!table_ready()) { t_skip("no GUI connection, no KernelCallbackTable"); return; }
    unsigned n = k_cb_count();
    t_note("consecutive non-NULL slots: %u", n);
    CHECK(n > 0);
    if (!r2_family()) { t_note("not a Windows 10 2004-family user32: count is Wine's own"); return; }
    /* win32kfull's largest ApiNumber is 0x80 (the default-window-proc callback), so at least 0x81 slots exist */
    CHECKF(n >= 0x7c, "k_cb_count()=%u", n);
    CHECKF(n >= 0x81, "k_cb_count()=%u, slot 0x80 (the default-window-proc callback) must be reachable", n);
}

R2T(table_count_is_exactly_129_then_null) {
    if (!table_ready()) { t_skip("no table"); return; }
    if (!r2_family()) { t_skip("Windows 10 2004 family only"); return; }
    EXPECT_EQ(k_cb_count(), R2_NREF);
    void *after = (void *)1;
    void *tbl = k_cb_table();
    CHECK(k_try_read_ptr((char *)tbl + (size_t)R2_NREF * sizeof(void *), &after));
    CHECK(after == NULL);
}

static void check_range(unsigned lo, unsigned hi) {
    if (!table_ready()) { t_skip("no table"); return; }
    if (!r2_family()) { t_skip("reference RVAs are for the Windows 10 user32"); return; }
    HMODULE u = r2_user32();
    for (unsigned i = lo; i < hi; i++) {
        void *p = slot_at(i);
        CHECKF(p != NULL && in_text(p), "slot %u (%s) = %p not inside user32 .text", i, r2_ref_name[i], p);
        if (r2_ref_revision())
            CHECKF((ULONG_PTR)p - (ULONG_PTR)u == r2_ref_rva[i], "slot %u (%s, kernel caller %s): rva 0x%lx want 0x%lx",
                   i, r2_ref_name[i], r2_ref_kfn[i][0] ? r2_ref_kfn[i] : "?", (unsigned long)((ULONG_PTR)p - (ULONG_PTR)u), r2_ref_rva[i]);
    }
    if (!r2_ref_revision()) t_note("revision is not 19041.2965: thunk RVAs not compared, only placement");
}
R2T(table_slots_00_to_09_message_thunks)  { check_range(0, 10); }
R2T(table_slots_10_to_19_struct_thunks)   { check_range(10, 20); }
R2T(table_slots_20_to_29_rect_string)     { check_range(20, 30); }
R2T(table_slots_30_to_39_out_thunks)      { check_range(30, 40); }
R2T(table_slots_40_to_49_hook_thunks)     { check_range(40, 50); }
R2T(table_slots_50_to_59_delegate_dpi)    { check_range(50, 60); }
R2T(table_slots_60_to_69_dde_image_lib)   { check_range(60, 70); }
R2T(table_slots_70_to_79_dde_load_text)   { check_range(70, 80); }
R2T(table_slots_80_to_89_font_thread_apc) { check_range(80, 90); }
R2T(table_slots_90_to_99_wow_ime_ole)     { check_range(90, 100); }
R2T(table_slots_100_to_109_dragdrop_uah)  { check_range(100, 110); }
R2T(table_slots_110_to_119_titlebar_touch){ check_range(110, 120); }
R2T(table_slots_120_to_128_mouse_theme_defproc) { check_range(120, 129); }

R2T(table_sharing_pairs_match_reference) {
    if (!table_ready()) { t_skip("no table"); return; }
    if (!r2_family()) { t_skip("Windows 10 2004 family only"); return; }
    /* The pairs are a property of the ABI (one user32 thunk serves several ApiNumbers), not of the revision. */
    unsigned eq = 0, ne = 0;
    for (unsigned i = 0; i < R2_NREF; i++)
        for (unsigned j = i + 1; j < R2_NREF; j++) {
            void *a = slot_at(i), *b = slot_at(j);
            if (r2_ref_rva[i] == r2_ref_rva[j]) { eq++; CHECKF(a == b, "slots %u and %u must share one thunk (%s)", i, j, r2_ref_name[i]); }
            else { ne++; CHECKF(a != b, "slots %u (%s) and %u (%s) must be distinct thunks", i, r2_ref_name[i], j, r2_ref_name[j]); }
        }
    t_note("%u equal pairs, %u distinct pairs", eq, ne);
    CHECK(eq > 0);
}

static void *ntcb_addr(void) { return (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCallbackReturn"); }

/* Looks for a rip-relative call/jmp (FF 15 / FF 25 / 48 FF 25) whose IAT slot holds `want`. */
static int thunk_returns_via(void *thunk, size_t limit, void *want) {
    unsigned char b[0x400];
    if (limit > sizeof b) limit = sizeof b;
    while (limit > 16 && !k_try_read(thunk, b, limit)) limit -= 16;
    if (limit <= 16) return 0;
    for (size_t i = 0; i + 7 <= limit; i++) {
        size_t instr_len = 0, disp_off = 0;
        if (b[i] == 0xFF && (b[i + 1] == 0x15 || b[i + 1] == 0x25)) { instr_len = 6; disp_off = 2; }
        else if (b[i] == 0x48 && b[i + 1] == 0xFF && b[i + 2] == 0x25) { instr_len = 7; disp_off = 3; }
        else continue;
        int disp; memcpy(&disp, b + i + disp_off, 4);
        void *iat = (char *)thunk + i + instr_len + disp, *val = NULL;
        if (k_try_read_ptr(iat, &val) && val == want) return 1;
    }
    return 0;
}

R2T(table_every_thunk_returns_through_ntcallbackreturn) {
    if (!table_ready()) { t_skip("no table"); return; }
    if (!r2_family()) { t_skip("Windows 10 2004 family only"); return; }
    void *want = ntcb_addr();
    REQUIRE(want);
    for (unsigned i = 0; i < R2_NREF; i++) {
        void *p = slot_at(i);
        CHECKF(p != NULL, "slot %u empty", i);
        if (!p) continue;
        /* a thunk ends where the next function starts: bound the scan by the nearest higher thunk address */
        ULONG_PTR end = (ULONG_PTR)p + 0x400;
        for (unsigned j = 0; j < R2_NREF; j++) { ULONG_PTR q = (ULONG_PTR)slot_at(j); if (q > (ULONG_PTR)p && q < end) end = q; }
        int ret = thunk_returns_via(p, end - (ULONG_PTR)p, want);
        if (i == 51 || i == 55) { CHECKF(!ret, "slot %u (%s) raises an exception, it must not call NtCallbackReturn", i, r2_ref_name[i]); continue; }
        CHECKF(ret, "slot %u (%s) at %p never calls NtCallbackReturn", i, r2_ref_name[i], p);
    }
}

R2T(table_dummy_callback_slots_are_51_and_55) {
    if (!table_ready()) { t_skip("no table"); return; }
    if (!r2_family()) { t_skip("Windows 10 2004 family only"); return; }
    /* __ClientCallDummyCallback builds an EXCEPTION_RECORD with code 0xC000001D (illegal instruction)
     * and RtlRaiseException()s it: slots reserved for callbacks win32kfull no longer makes. */
    CHECK(slot_at(51) == slot_at(55));
    for (unsigned i = 0; i < R2_NREF; i++)
        if (i != 51 && i != 55) CHECKF(slot_at(i) != slot_at(51), "slot %u unexpectedly shares the dummy callback", i);
    unsigned char b[0x80];
    REQUIRE(k_try_read(slot_at(51), b, sizeof b));
    int found = 0;
    for (size_t i = 0; i + 4 <= sizeof b; i++) { unsigned v; memcpy(&v, b + i, 4); if (v == 0xC000001Du) found = 1; }
    CHECKF(found, "the dummy thunk must load the 0xC000001D exception code");
}

R2T(table_page_is_read_only_image_memory) {
    if (!table_ready()) { t_skip("no table"); return; }
    if (!r2_family()) { t_skip("Windows 10 2004 family only"); return; }
    void *tbl = k_cb_table();
    MEMORY_BASIC_INFORMATION mbi;
    REQUIRE(VirtualQuery(tbl, &mbi, sizeof mbi));
    CHECKF(mbi.Type == MEM_IMAGE, "Type=0x%lx", (unsigned long)mbi.Type);
    CHECKF(mbi.Protect == PAGE_READONLY, "Protect=0x%lx (apfnDispatch lives in user32 .rdata)", (unsigned long)mbi.Protect);
    CHECK((char *)mbi.AllocationBase == (char *)r2_user32());
    if (r2_ref_revision()) EXPECT_EQ((ULONG_PTR)tbl - (ULONG_PTR)r2_user32(), 0x8f070);
}

R2T(table_values_stable_across_gui_activity) {
    if (!table_ready()) { t_skip("no table"); return; }
    unsigned n = k_cb_count();
    void *before[R2_NSLOT]; unsigned m = n < R2_NSLOT ? n : R2_NSLOT;
    for (unsigned i = 0; i < m; i++) before[i] = slot_at(i);
    void *tbl = k_cb_table();
    for (int round = 0; round < 4; round++) {
        HWND h = k_make_window("r2cb-stable");
        if (h) { k_pump(5); SetWindowTextW(h, L"changed"); DestroyWindow(h); }
        k_pump(5);
    }
    EXPECT_EQ(k_cb_table(), tbl);
    unsigned diff = 0;
    for (unsigned i = 0; i < m; i++) if (slot_at(i) != before[i]) diff++;
    CHECKF(diff == 0, "%u slots changed during window create/destroy cycles", diff);
}

R2T(table_dispatcher_indexes_peb_table_unchecked) {
    /* The ApiNumber is trusted - no bounds check exists on the user-mode side - so the kernel is the only
     * thing keeping indexes inside the 129 live slots. */
    FARPROC f = GetProcAddress(GetModuleHandleA("ntdll.dll"), "KiUserCallbackDispatcher");
    if (!f) { t_skip("KiUserCallbackDispatcher not exported"); return; }
    if (!r2_family()) { t_skip("Windows 10 2004 family only"); return; }
    unsigned char b[0x40];
    REQUIRE(k_try_read((void *)f, b, sizeof b));
    static const unsigned char gs60[] = { 0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00 };   /* mov rax, gs:[60h] */
    static const unsigned char tbl58[] = { 0x4C, 0x8B, 0x48, 0x58 };                              /* mov r9, [rax+58h] */
    static const unsigned char idx8[] = { 0x4B, 0x8B, 0x04, 0xC1 };                                /* mov rax, [r9+r8*8] */
    int a = 0, c = 0, d = 0;
    for (size_t i = 0; i + 9 <= sizeof b; i++) if (!memcmp(b + i, gs60, sizeof gs60)) a = 1;
    for (size_t i = 0; i + 4 <= sizeof b; i++) { if (!memcmp(b + i, tbl58, 4)) c = 1; if (!memcmp(b + i, idx8, 4)) d = 1; }
    if (r2_ref_revision()) { CHECK(a); CHECK(c); CHECK(d); }
    else { t_note("dispatcher shape: peb=%d table=%d index=%d", a, c, d); CHECK(a || c || d); }
    /* the first three instructions load the dispatcher arguments from the callback frame */
    static const unsigned char args[] = { 0x48, 0x8B, 0x4C, 0x24, 0x20, 0x8B, 0x54, 0x24, 0x28, 0x44, 0x8B, 0x44, 0x24, 0x2C };
    if (r2_ref_revision()) CHECK(!memcmp(b, args, sizeof args));
}

R2T(table_kernel_caller_names_cover_the_table) {
    /* r2cb_data.h r2_ref_kfn: most ApiNumbers have an analysed win32kfull caller whose
     * KeUserModeCallback number equals the slot index; the rest are shared names or unused slots. */
    unsigned named = 0;
    for (unsigned i = 0; i < R2_NREF; i++) if (r2_ref_kfn[i][0]) named++;
    t_note("%u of %u slots have a known kernel caller", named, R2_NREF);
    CHECK(named >= 95);
    /* user32-side names agree with the kernel-side names for the Client entries */
    for (unsigned i = 0; i < R2_NREF; i++) {
        const char *k = r2_ref_kfn[i];
        if (!k[0]) continue;
        if (!strncmp(k, "Client", 6)) {
            const char *u = r2_ref_name[i];
                        const char *uu = !strncmp(u, "__", 2) ? u + 2 : u;
            CHECKF(!strcmp(k, uu), "slot %u: kernel %s vs user32 %s", i, k, u);
        }
    }
}
