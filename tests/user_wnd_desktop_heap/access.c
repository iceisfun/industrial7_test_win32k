/* user_wnd_desktop_heap: how the client reaches the read-only window structures
 * (handle entry -> phead + ulClientDelta -> tagWND), the DESKTOPINFO the TEB points at, and the
 * properties of the mapped heap itself. */
#define WDH_TAG L"a"
#include "wdh.h"

#define P "user_wnd_desktop_heap_"

/* ---- handle entry ---------------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_entry_type_and_uniq) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* user32!HMValidateHandle accepts a handle when entry+0x18 == requested type (1 for HWND),
     * entry+0x19 bit0 is clear and entry+0x1a == HIWORD(handle) (or 0xffff). */
    EXPECT_EQ(w.e.type, 1);
    CHECK((w.e.flags & 1) == 0);
    CHECKF(w.e.uniq == (unsigned short)((ULONG_PTR)h >> 16) || w.e.uniq == 0xffff, "uniq=%04x hwnd=%p", w.e.uniq, h);
    END();
}
TEST(user_wnd_desktop_heap_entry_index_below_count) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* every user32 validator starts with (hwnd & 0xffff) < *(ULONG64*)(shared server info + 8) */
    CHECKF((uint64_t)((ULONG_PTR)h & 0xffff) < w.e.count, "index %lu count %llu", (unsigned long)((ULONG_PTR)h & 0xffff), (unsigned long long)w.e.count);
    CHECK(w.e.count > 0 && w.e.count <= 0x10000);
    t_info("cHandleEntries=%llu HeEntrySize=0x%lx", (unsigned long long)w.e.count, w.e.esize);
    END();
}
TEST(user_wnd_desktop_heap_entry_size_covers_fields) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* the validators read entry+0x1a (a USHORT), so an entry is at least 0x1c bytes; the
     * multiplier user32 caches comes from gSharedInfo+0x10 */
    CHECK(w.e.esize >= 0x1c);
    if (w.e.esize & 7) t_note("HeEntrySize 0x%lx is not a multiple of 8", w.e.esize);
    END();
}
TEST(user_wnd_desktop_heap_entry_owner_is_creating_pti) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* _GetWindowLong compares entry+8 with PtiCurrent() (= TEB+0x78) to decide whether extra bytes
     * can be read directly, so for a window this thread created they are equal. */
    uint64_t pti = pti_now();
    CHECK(pti != 0);
    EXPECT_EQ(w.e.owner, pti);
    END();
}
TEST(user_wnd_desktop_heap_entry_owner_other_thread) {
    T_BEGIN();
    if (!plain_class()) { t_skip("class registration failed"); return; }
    struct xt x;
    if (!xt_start(&x, plain_class())) { xt_stop(&x); t_skip("helper thread window failed"); return; }
    NEED_WND(x.hwnd, w);
    uint64_t other = thread_pti(x.tid);
    CHECK(other != 0);
    CHECK(other != pti_now());
    EXPECT_EQ(w.e.owner, other);
    DWORD pid = 0;
    DWORD tid = U(GetWindowThreadProcessId)(x.hwnd, &pid);
    EXPECT_EQ(tid, x.tid);
    EXPECT_EQ(pid, GetCurrentProcessId());
    xt_stop(&x);
    END();
}
TEST(user_wnd_desktop_heap_entry_owner_of_desktop_window) {
    T_BEGIN();
    HWND d = U(GetDesktopWindow)();
    NEED_WND(d, w);
    /* the desktop window belongs to the desktop's own thread, never to this one */
    CHECK(w.e.owner != 0);
    CHECK(w.e.owner != pti_now());
    END();
}
TEST(user_wnd_desktop_heap_entry_guard_matches_deskinfo) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    uint64_t di = deskinfo_now(), base = 0;
    CHECK(di != 0);
    CHECK(wv_read_at(di + DI_BASE, &base, 8));
    /* HMValidateHandle takes the no-syscall path only if entry+0x10 == *(*(TEB+0x820)) */
    EXPECT_EQ(w.e.guard, base);
    END();
}
TEST(user_wnd_desktop_heap_entry_free_slot_not_window) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    HWND stale = h;
    CHECK(U(DestroyWindow)(h));
    CHECK(!U(IsWindow)(stale));
    struct wv w2;
    /* a destroyed handle must no longer decode as a live window */
    CHECK(!wv_get(stale, &w2));
    struct he e;
    if (he_read(stale, &e)) t_info("destroyed entry: type=%u flags=0x%02x uniq=0x%04x", e.type, e.flags, e.uniq);
    END();
}
TEST(user_wnd_desktop_heap_entry_handle_reuse_bumps_uniq) {
    T_BEGIN();
    HWND a = mk_plain();
    NEED_WND(a, w);
    U(DestroyWindow)(a);
    HWND b = mk_plain();
    if (!b) { t_skip("recreate failed"); return; }
    if (((ULONG_PTR)b & 0xffff) != ((ULONG_PTR)a & 0xffff)) {
        t_note("slot not reused (%p -> %p)", a, b);
        END();
        return;
    }
    /* same table slot: the unique word must differ or the stale handle would validate again */
    CHECKF(b != a, "handle %p reused verbatim", a);
    CHECK(!U(IsWindow)(a) || a == b);
    CHECK(U(IsWindow)(b));
    END();
}

/* ---- client pointer -------------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_client_ptr_is_phead_plus_delta) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* fast path of every user32 getter: *(TEB+0x828) + entry[0] */
    EXPECT_EQ(w.pc, w.e.phead + delta_now());
    CHECK(w.delta != 0);
    END();
}
TEST(user_wnd_desktop_heap_client_ptr_is_user_mode) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    CHECKF(w.e.phead != 0 && w.e.phead < 0x80000000ULL, "phead 0x%llx", (unsigned long long)w.e.phead);   /* an offset into the desktop heap, not an address */
    CHECKF(w.pc < 0x00007FFFFFFFFFFFULL, "client pointer 0x%llx", (unsigned long long)w.pc);
    END();
}
TEST(user_wnd_desktop_heap_head_hwnd_equals_handle) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* GetWindow returns *(HWND*)pwnd */
    EXPECT_EQ(W64(&w, W_HWND), (ULONG_PTR)h);
    END();
}
TEST(user_wnd_desktop_heap_pself_equals_entry_phead) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* pointers inside the structure are rebased as p + (k - *(p+8)); that only yields client
     * addresses if *(p+8) is the kernel address the handle entry holds */
    EXPECT_EQ(W64(&w, W_PSELF), w.e.phead);
    END();
}
TEST(user_wnd_desktop_heap_pself_equal_for_child) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 1, 1, 10, 10, 5);
    NEED_WND(c, w);
    EXPECT_EQ(W64(&w, W_PSELF), w.e.phead);
    EXPECT_EQ(W64(&w, W_HWND), (ULONG_PTR)c);
    END();
}
TEST(user_wnd_desktop_heap_callone_0x38_returns_client_ptr) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    REQUIRE(UN(NtUserCallOneParam));
    /* user32 calls NtUserCallOneParam(hwnd, 0x38) whenever the fast path is unusable and uses the
     * result as the tagWND*; it must be the very pointer the fast path computes */
    ULONG_PTR r = UN(NtUserCallOneParam)((ULONG_PTR)h, 0x38);
    EXPECT_EQ(r, w.pc);
    END();
}
TEST(user_wnd_desktop_heap_callone_0x38_desktop_window) {
    T_BEGIN();
    HWND d = U(GetDesktopWindow)();
    NEED_WND(d, w);
    REQUIRE(UN(NtUserCallOneParam));
    EXPECT_EQ(UN(NtUserCallOneParam)((ULONG_PTR)d, 0x38), w.pc);
    END();
}
TEST(user_wnd_desktop_heap_callone_0x38_foreign_thread_window) {
    T_BEGIN();
    if (!plain_class()) { t_skip("class registration failed"); return; }
    REQUIRE(UN(NtUserCallOneParam));
    struct xt x;
    if (!xt_start(&x, plain_class())) { xt_stop(&x); t_skip("helper thread window failed"); return; }
    NEED_WND(x.hwnd, w);
    EXPECT_EQ(UN(NtUserCallOneParam)((ULONG_PTR)x.hwnd, 0x38), w.pc);
    xt_stop(&x);
    END();
}
TEST(user_wnd_desktop_heap_callone_0x38_invalid_handles) {
    T_BEGIN();
    REQUIRE(UN(NtUserCallOneParam));
    HWND h = mk_plain();
    if (!h) { t_skip("window creation failed"); return; }
    HWND stale = h;
    U(DestroyWindow)(h);
    /* the kernel routine validates the handle (ValidateHwnd) and yields 0 for anything else */
    HWND bad[] = { NULL, stale, (HWND)(ULONG_PTR)0xFFFFFFFF, (HWND)(ULONG_PTR)0x7FFF, (HWND)(ULONG_PTR)0xFFFF0001, (HWND)(ULONG_PTR)1 };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        ULONG_PTR r = UN(NtUserCallOneParam)((ULONG_PTR)bad[i], 0x38);
        CHECKF(r == 0, "handle %p -> 0x%llx", bad[i], (unsigned long long)r);
    }
    END();
}
TEST(user_wnd_desktop_heap_callone_0x38_non_window_handle) {
    T_BEGIN();
    REQUIRE(UN(NtUserCallOneParam));
    REQUIRE(U(CreatePopupMenu));
    HMENU m = U(CreatePopupMenu)();
    if (!m) { t_skip("menu creation failed"); return; }
    /* the routine does not insist on a window: for a menu it answers the client address of the menu object, which is
     * the entry's heap offset plus the client delta, like it does for a window */
    struct he e;
    ULONG_PTR r = UN(NtUserCallOneParam)((ULONG_PTR)m, 0x38);
    if (he_read((HWND)m, &e) && e.type == 2) CHECKF(r == e.phead + delta_now(), "result 0x%llx", (unsigned long long)r);
    else CHECK(r != 0);
    U(DestroyMenu)(m);
}
TEST(user_wnd_desktop_heap_invalid_hwnd_last_error_1400) {
    T_BEGIN();
    HWND h = mk_plain();
    if (!h) { t_skip("window creation failed"); return; }
    HWND stale = h;
    U(DestroyWindow)(h);
    /* ValidateHwnd -> RtlSetLastWin32Error(0x578) in user32 */
    SET_ERR(0);
    LONG_PTR v = U(GetWindowLongPtrW)(stale, GWLP_USERDATA);
    CHECK(v == 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    RECT r;
    SET_ERR(0);
    CHECK(!U(GetWindowRect)(stale, &r));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    SET_ERR(0);
    CHECK(!U(GetClientRect)(stale, &r));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    END();
}

/* ---- NtUserQueryWindow ----------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_querywindow_pid_tid_own) {
    T_BEGIN();
    HWND h = mk_plain();
    if (!h) { t_skip("window creation failed"); return; }
    REQUIRE(UN(NtUserQueryWindow));
    /* GetWindowThreadProcessId calls NtUserQueryWindow(h, 0) for the pid and (h, 2) for the tid */
    EXPECT_EQ(UN(NtUserQueryWindow)(h, 0), GetCurrentProcessId());
    EXPECT_EQ(UN(NtUserQueryWindow)(h, 1), GetCurrentProcessId());
    EXPECT_EQ(UN(NtUserQueryWindow)(h, 2), GetCurrentThreadId());
    END();
}
TEST(user_wnd_desktop_heap_querywindow_other_thread) {
    T_BEGIN();
    if (!plain_class()) { t_skip("class registration failed"); return; }
    REQUIRE(UN(NtUserQueryWindow));
    struct xt x;
    if (!xt_start(&x, plain_class())) { xt_stop(&x); t_skip("helper thread window failed"); return; }
    EXPECT_EQ(UN(NtUserQueryWindow)(x.hwnd, 0), GetCurrentProcessId());
    EXPECT_EQ(UN(NtUserQueryWindow)(x.hwnd, 2), x.tid);
    DWORD pid = 0;
    EXPECT_EQ(U(GetWindowThreadProcessId)(x.hwnd, &pid), x.tid);
    EXPECT_EQ(pid, GetCurrentProcessId());
    xt_stop(&x);
    END();
}
TEST(user_wnd_desktop_heap_querywindow_invalid_handle_is_zero) {
    T_BEGIN();
    REQUIRE(UN(NtUserQueryWindow));
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0xFFFFFFFF, (HWND)(ULONG_PTR)0x12345678, (HWND)(ULONG_PTR)2 };
    for (int idx = 0; idx <= 2; idx++)
        for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
            ULONG_PTR r = UN(NtUserQueryWindow)(bad[i], idx);
            CHECKF(r == 0, "handle %p index %d -> 0x%llx", bad[i], idx, (unsigned long long)r);
        }
}
TEST(user_wnd_desktop_heap_querywindow_unknown_index_is_zero) {
    T_BEGIN();
    HWND h = mk_plain();
    if (!h) { t_skip("window creation failed"); return; }
    REQUIRE(UN(NtUserQueryWindow));
    /* the switch in win32kfull!NtUserQueryWindow has no case 6 and nothing above 9 */
    DWORD idx[] = { 6, 10, 11, 0x20, 0x7f, 0xffff, 0x80000000u, 0xffffffffu };
    for (size_t i = 0; i < sizeof idx / sizeof *idx; i++) {
        ULONG_PTR r = UN(NtUserQueryWindow)(h, idx[i]);
        CHECKF(r == 0, "index 0x%lx -> 0x%llx", (unsigned long)idx[i], (unsigned long long)r);
    }
    END();
}
TEST(user_wnd_desktop_heap_querywindow_hung_and_foreground_flags) {
    T_BEGIN();
    HWND h = mk_plain();
    if (!h) { t_skip("window creation failed"); return; }
    REQUIRE(UN(NtUserQueryWindow));
    /* case 5 yields IsHungWindow (0/1; a thread that has not read its queue for a few seconds counts as
     * hung, so only the range is fixed), case 7 a boolean */
    CHECK(UN(NtUserQueryWindow)(h, 5) <= 1);
    ULONG_PTR fg = UN(NtUserQueryWindow)(h, 7);
    CHECK(fg == 0 || fg == 1);
    END();
}

/* ---- DESKTOPINFO ----------------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_deskinfo_pointer_readable) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    uint64_t di = deskinfo_now(), q[4] = {0};
    CHECK(di != 0);
    CHECK(wv_read_at(di, q, sizeof q));
    CHECK(q[0] != 0);   /* pvDesktopBase */
    CHECK(q[1] != 0);   /* spwnd */
    END();
}
TEST(user_wnd_desktop_heap_deskinfo_spwnd_is_desktop_window) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    uint64_t di = deskinfo_now(), sp = 0, hw = 0;
    CHECK(wv_read_at(di + DI_SPWND, &sp, 8));
    /* GetCurrentThreadDesktopWindow = *(di+8) + ulClientDelta; GetDesktopWindow reads the HWND there */
    CHECK(wv_read_at(sp + w.delta, &hw, 8));
    EXPECT_EQ((uint32_t)hw, (uint32_t)(ULONG_PTR)U(GetDesktopWindow)());
    END();
}
TEST(user_wnd_desktop_heap_deskinfo_desktop_window_fnid) {
    T_BEGIN();
    HWND d = U(GetDesktopWindow)();
    NEED_WND(d, w);
    /* GetWindowRect/GetClientRect/IsWindowVisible special-case (fnid & 0x2fff) == 0x29d */
    EXPECT_EQ(wv_fnid(&w), FNID_DESKTOP);
    END();
}
TEST(user_wnd_desktop_heap_deskinfo_desktop_window_has_no_parent) {
    T_BEGIN();
    HWND d = U(GetDesktopWindow)();
    NEED_WND(d, w);
    EXPECT_EQ(W64(&w, W_PARENT), 0);
    CHECK(U(GetParent)(d) == NULL);
    CHECK(U(GetAncestor)(d, GA_PARENT) == NULL);
    END();
}
TEST(user_wnd_desktop_heap_deskinfo_desktop_window_visible) {
    T_BEGIN();
    HWND d = U(GetDesktopWindow)();
    NEED_WND(d, w);
    /* IsWindowVisible returns TRUE as soon as the walk reaches the desktop window */
    CHECK(U(IsWindowVisible)(d));
    CHECK((wv_style(&w) & WS_VISIBLE) != 0 || (wv_ex(&w) & 0x800) != 0);
    END();
}
TEST(user_wnd_desktop_heap_deskinfo_desktop_class_name) {
    T_BEGIN();
    HWND d = U(GetDesktopWindow)();
    if (!d) { t_skip("no desktop window"); return; }
    wchar_t nm[64] = L"";
    if (t_is_wine()) { t_skip("Wine desktop window class differs"); return; }
    int n = U(GetClassNameW)(d, nm, 64);
    /* the desktop window class is the integer atom #32769 */
    CHECK(n > 0);
    EXPECT_WSTR(nm, L"#32769");
}
TEST(user_wnd_desktop_heap_deskinfo_shell_pointer_matches_api) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    uint64_t di = deskinfo_now(), sk = 0;
    CHECK(wv_read_at(di + DI_SHELL, &sk, 8));
    HWND want = NULL;
    if (sk) { uint64_t v = 0; if (wv_read_at(sk + w.delta, &v, 8)) want = (HWND)(ULONG_PTR)v; }
    /* GetShellWindow = *(*(di+0x18) + ulClientDelta) or 0 */
    EXPECT_EQ((ULONG_PTR)U(GetShellWindow)(), (ULONG_PTR)want);
    if (!want) t_note("no shell window on this desktop");
    END();
}
TEST(user_wnd_desktop_heap_deskinfo_shell_window_is_toplevel) {
    T_BEGIN();
    HWND s = U(GetShellWindow)();
    if (!s) { t_skip("no shell window"); return; }
    NEED_WND(s, w);
    HWND d = U(GetDesktopWindow)();
    EXPECT_EQ((ULONG_PTR)wv_link(&w, W_PARENT), (ULONG_PTR)d);
    CHECK(U(IsWindow)(s));
    END();
}
TEST(user_wnd_desktop_heap_deskinfo_desktop_window_pself) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    uint64_t sp = 0;
    CHECK(wv_read_at(deskinfo_now() + DI_SPWND, &sp, 8));
    struct wv dw;
    CHECK(wv_get(U(GetDesktopWindow)(), &dw));
    /* DESKTOPINFO.spwnd is the same kernel address as the desktop window's own phead/pSelf */
    EXPECT_EQ(sp, dw.e.phead);
    EXPECT_EQ(sp, W64(&dw, W_PSELF));
    END();
}

/* ---- the mapped heap ------------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_heap_is_committed_mapped_view) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    MEMORY_BASIC_INFORMATION m;
    CHECK(vq(w.pc, &m));
    EXPECT_EQ(m.State, MEM_COMMIT);
    EXPECT_EQ(m.Type, MEM_MAPPED);   /* section view of the desktop heap */
    CHECK(m.AllocationBase != NULL);
    END();
}
TEST(user_wnd_desktop_heap_heap_is_not_writable) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    MEMORY_BASIC_INFORMATION m;
    CHECK(vq(w.pc, &m));
    /* user mode only gets a read-only view of the desktop heap */
    DWORD wr = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY | PAGE_EXECUTE | PAGE_EXECUTE_READ;
    CHECKF((m.Protect & wr) == 0, "protect=0x%lx", (unsigned long)m.Protect);
    CHECK((m.Protect & (PAGE_READONLY)) != 0);
    END();
}
TEST(user_wnd_desktop_heap_heap_window_and_class_in_same_view) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    CHECK(same_view(w.pc, c.pc));
    END();
}
TEST(user_wnd_desktop_heap_heap_all_windows_share_one_view) {
    T_BEGIN();
    HWND a = mk_plain();
    HWND b = mk_plain();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 0, 0, 5, 5, 1);
    struct wv wa, wb, wp, wc;
    NEED_WND(a, w0); (void)w0;
    CHECK(wv_get(a, &wa) && wv_get(b, &wb) && wv_get(p, &wp) && wv_get(c, &wc));
    CHECK(same_view(wa.pc, wb.pc));
    CHECK(same_view(wa.pc, wp.pc));
    CHECK(same_view(wa.pc, wc.pc));
    struct wv dw;
    CHECK(wv_get(U(GetDesktopWindow)(), &dw));
    CHECK(same_view(wa.pc, dw.pc));
    END();
}
TEST(user_wnd_desktop_heap_heap_view_base_vs_deskinfo_base) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    MEMORY_BASIC_INFORMATION m;
    CHECK(vq(w.pc, &m));
    uint64_t base = 0;
    CHECK(wv_read_at(deskinfo_now() + DI_BASE, &base, 8));
    /* DESKTOPINFO's first qword is the handle-entry guard; whether it is also the kernel base of the
     * heap is only observed here: kernel base + ulClientDelta should fall inside the client view. */
    uint64_t cbase = base + w.delta;
    uint64_t lo = (uint64_t)(ULONG_PTR)m.AllocationBase;
    t_note("view base=%p guard+delta=0x%llx wnd=0x%llx delta=0x%llx -> %s", m.AllocationBase, (unsigned long long)cbase,
           (unsigned long long)w.pc, (unsigned long long)w.delta, (cbase >= lo && cbase <= w.pc) ? "inside" : "outside");
    END();
}
TEST(user_wnd_desktop_heap_heap_window_pointer_within_kernel_heap_range) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    uint64_t base = 0;
    CHECK(wv_read_at(deskinfo_now() + DI_BASE, &base, 8));
    /* every pointer user32 rebases is only meaningful relative to the same heap: the window and the
     * desktop window sit in one kernel address window no larger than the client view */
    struct wv dw;
    CHECK(wv_get(U(GetDesktopWindow)(), &dw));
    uint64_t d = w.e.phead > dw.e.phead ? w.e.phead - dw.e.phead : dw.e.phead - w.e.phead;
    uint64_t cd = w.pc > dw.pc ? w.pc - dw.pc : dw.pc - w.pc;
    EXPECT_EQ(d, cd);   /* same delta for both: kernel and client distances are identical */
    CHECK(d < 0x40000000ULL);
    END();
}
TEST(user_wnd_desktop_heap_heap_structure_bytes_stable_while_idle) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    struct wv w2;
    k_pump(20);
    CHECK(wv_get(h, &w2));
    /* hidden window, no messages in between: geometry/style/links are unchanged */
    EXPECT_EQ(wv_style(&w), wv_style(&w2));
    EXPECT_EQ(wv_ex(&w), wv_ex(&w2));
    EXPECT_EQ(W64(&w, W_PARENT), W64(&w2, W_PARENT));
    CHECK(memcmp(w.b + W_RCWINDOW, w2.b + W_RCWINDOW, 32) == 0);
    END();
}

/* ---- the TEB callback-window cache ------------------------------------------------------------ */

TEST(user_wnd_desktop_heap_cbwnd_cache_inside_wndproc) {
    T_BEGIN();
    if (!plain_class()) { t_skip("class registration failed"); return; }
    memset(&g_rec, 0, sizeof g_rec);
    g_rec.on = 1;
    HWND h = mk_plain();
    g_rec.on = 0;
    if (!h) { t_skip("window creation failed"); return; }
    NEED_WND(h, w);
    CHECK(g_rec.n >= 2);   /* at least WM_NCCREATE and WM_CREATE */
    int seen = 0;
    for (int i = 0; i < g_rec.n; i++) {
        if (g_rec.wnd[i] != h) continue;
        /* user32's fast path trusts "hWnd == *(TEB+0x840) -> pwnd = *(TEB+0x848)"; whenever the
         * kernel has published a callback window it must be this window and its client pointer */
        if (g_rec.cache_h[i]) {
            seen++;
            EXPECT_EQ((uint32_t)g_rec.cache_h[i], (uint32_t)(ULONG_PTR)h);
            EXPECT_EQ(g_rec.cache_p[i], g_rec.pwnd[i]);
        }
    }
    t_note("%d of %d recorded callbacks had the TEB callback-window cache populated", seen, g_rec.n);
    END();
}
TEST(user_wnd_desktop_heap_cbwnd_cache_on_sendmessage) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    memset(&g_rec, 0, sizeof g_rec);
    g_rec.on = 1;
    U(SendMessageW)(h, WM_USER + 5, 0, 0);
    U(SendMessageW)(h, WM_USER + 6, 0, 0);
    g_rec.on = 0;
    CHECK(g_rec.n >= 2);
    for (int i = 0; i < g_rec.n; i++) {
        CHECK(g_rec.wnd[i] == h);
        if (g_rec.cache_h[i]) {
            EXPECT_EQ((uint32_t)g_rec.cache_h[i], (uint32_t)(ULONG_PTR)h);
            EXPECT_EQ(g_rec.cache_p[i], w.pc);
        }
    }
    END();
}
TEST(user_wnd_desktop_heap_cbwnd_cache_other_window_not_confused) {
    T_BEGIN();
    HWND a = mk_plain();
    HWND b = mk_plain();
    NEED_WND(a, wa);
    NEED_WND(b, wb);
    memset(&g_rec, 0, sizeof g_rec);
    g_rec.on = 1;
    U(SendMessageW)(a, WM_USER + 1, 0, 0);
    U(SendMessageW)(b, WM_USER + 1, 0, 0);
    g_rec.on = 0;
    for (int i = 0; i < g_rec.n; i++) {
        if (!g_rec.cache_h[i]) continue;
        uint64_t want = g_rec.wnd[i] == a ? wa.pc : wb.pc;
        EXPECT_EQ((uint32_t)g_rec.cache_h[i], (uint32_t)(ULONG_PTR)g_rec.wnd[i]);
        EXPECT_EQ(g_rec.cache_p[i], want);
    }
    END();
}

/* ---- pristine process ------------------------------------------------------------------------- */

CHILD(wdh_fresh_connect) {
    (void)argc; (void)argv;
    int fails = 0;
    HMODULE u = LoadLibraryA("user32.dll");
    if (!u) { printf("FAIL cannot load user32\n"); return 1; }
    HWND (WINAPI *gdw)(void) = (HWND (WINAPI *)(void))GetProcAddress(u, "GetDesktopWindow");
    HWND d = gdw ? gdw() : NULL;
    /* the first win32k call converts the thread; afterwards the TEB carries pti, DESKTOPINFO, delta */
    if (!d) { printf("NOTE no desktop window in this child (no interactive desktop?)\n"); return 0; }
    struct wv w;
    if (!wv_get(d, &w)) { printf("NOTE desktop window not decodable (not a Windows desktop heap)\n"); return 0; }
    uint64_t pti = pti_now(), di = deskinfo_now(), dl = delta_now();
    if (!pti) { printf("FAIL TEB.Win32ThreadInfo still 0\n"); fails++; }
    if (!di) { printf("FAIL TEB DESKTOPINFO pointer still 0\n"); fails++; }
    if (!dl) { printf("FAIL ulClientDelta still 0\n"); fails++; }
    if (wv_fnid(&w) != FNID_DESKTOP) { printf("FAIL desktop fnid 0x%x\n", wv_fnid(&w)); fails++; }
    if (W64(&w, W_PSELF) != w.e.phead) { printf("FAIL pSelf != phead\n"); fails++; }
    if (w.e.owner == pti) { printf("FAIL desktop window owned by this thread\n"); fails++; }
    return fails;
}
TEST(user_wnd_desktop_heap_fresh_process_first_call_populates_teb) {
    if (t_is_wine()) { t_skip("second GUI process hangs behind the parent's session under headless Wine"); return; }
    struct k_child_result r = k_child_run("wdh_fresh_connect", NULL, 20000);
    EXPECT_EQ(r.exit_code, 0);
}
CHILD(wdh_fresh_window) {
    (void)argc; (void)argv;
    int fails = 0;
    HMODULE u = LoadLibraryA("user32.dll");
    if (!u) return 1;
    plain_class();
    HWND h = mk_plain();
    if (!h) { printf("NOTE cannot create window\n"); return 0; }
    struct wv w;
    if (!wv_get(h, &w)) { printf("NOTE window not decodable\n"); return 0; }
    if (w.e.owner != pti_now() || !pti_now()) { printf("FAIL owner != pti\n"); fails++; }
    if (w.e.guard != 0) {
        uint64_t base = 0;
        wv_read_at(deskinfo_now() + DI_BASE, &base, 8);
        if (w.e.guard != base) { printf("FAIL guard != pvDesktopBase\n"); fails++; }
    }
    if (w.pc != w.e.phead + delta_now()) { printf("FAIL client pointer mismatch\n"); fails++; }
    return fails;
}
TEST(user_wnd_desktop_heap_fresh_process_first_window_decodes) {
    if (t_is_wine()) { t_skip("second GUI process hangs behind the parent's session under headless Wine"); return; }
    struct k_child_result r = k_child_run("wdh_fresh_window", NULL, 20000);
    EXPECT_EQ(r.exit_code, 0);
}
