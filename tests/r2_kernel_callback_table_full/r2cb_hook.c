/* r2_kernel_callback_table_full: the private-table hook itself, and what the dispatcher hands every
 * callback (registers, event ordering, harmlessness of padding slots, clean restore).
 *
 * Every scenario runs in a CHILD: it copies user32!apfnDispatch into a private page, puts a recording
 * stub in every slot and patches PEB+0x58. The kernel (ntdll!KiUserCallbackDispatcher) re-reads
 * PEB->KernelCallbackTable on each callback, so the copy is what it calls; r2_uninstall() (also atexit)
 * writes the original pointer back.
 */
#include "r2cb.h"

#define NEED_HOOK() do { R2_NEED(r2_gui_connect(), "no GUI connection / KernelCallbackTable"); \
                         R2_NEED(r2_install(), "cannot install the private callback table"); } while (0)

/* A broad but quiet GUI workload: windows, text, position, a cross-thread send, destroy. */
static void workload(struct r2_wk *w) {
    for (int i = 0; i < 3; i++) {
        HWND h = k_make_window("r2cb-workload");
        if (h) {
            SetWindowTextW(h, L"r2cb-work");
            SetWindowPos(h, NULL, -32000, -32000, 40 + i, 30 + i, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            k_pump(5);
            DestroyWindow(h);
        }
    }
    if (w && w->hwnd) {
        LRESULT r;
        r2_send(w->hwnd, WM_USER + 1, 1, 2, &r);
        r2_send(w->hwnd, WM_USER + 2, 3, 4, &r);
    }
    k_pump(10);
}

R2_SCN(hook_install_swaps_peb_pointer_and_leaves_user32_table_untouched) {
    R2_NEED(r2_gui_connect(), "no GUI connection");
    void *real = k_cb_table();
    unsigned n = k_cb_count();
    R2_NEED(real && n, "no table");
    void *snap[R2_NSLOT];
    for (unsigned i = 0; i < n && i < R2_NSLOT; i++) k_try_read_ptr((char *)real + (size_t)i * 8, &snap[i]);
    R2_NEED(r2_install(), "cannot install");
    void *act = k_cb_table();
    K(act == r2_active_table());
    K(act != real);
    K(((ULONG_PTR)act & 0xFFF) == 0);                    /* a private page of its own */
    K(r2_saved_table() == real);
    KEQ(r2_nlive, n);
    unsigned moved = 0, kept = 0, same_orig = 0;
    for (unsigned i = 0; i < n && i < R2_NSLOT; i++) {
        void *now = NULL, *old = NULL;
        k_try_read_ptr((char *)act + (size_t)i * 8, &now);
        k_try_read_ptr((char *)real + (size_t)i * 8, &old);
        if (now != snap[i]) moved++;
        if (old == snap[i]) kept++;
        if (r2_orig[i] == snap[i]) same_orig++;
    }
    KEQ(moved, n);          /* every live slot now points at a recording stub */
    KEQ(kept, n);           /* user32's own read-only table is not modified (it cannot be) */
    KEQ(same_orig, n);      /* the original entries are remembered exactly */
    r2_uninstall();
    K(k_cb_table() == real);
    K(!r2_installed());
    unsigned back = 0;
    for (unsigned i = 0; i < n && i < R2_NSLOT; i++) { void *p = NULL; k_try_read_ptr((char *)real + (size_t)i * 8, &p); if (p == snap[i]) back++; }
    KEQ(back, n);
    return 0;
}

R2_SCN(hook_stubs_are_distinct_executable_and_cover_padding) {
    NEED_HOOK();
    void **act = (void **)r2_active_table();
    unsigned bad = 0;
    for (unsigned i = 0; i < R2_NSLOT; i++) {
        void *s = NULL; k_try_read_ptr(act + i, &s);
        MEMORY_BASIC_INFORMATION mbi;
        if (!s || !VirtualQuery(s, &mbi, sizeof mbi) || mbi.Protect != PAGE_EXECUTE_READWRITE) bad++;
        for (unsigned j = 0; j < i; j++) { void *t = NULL; k_try_read_ptr(act + j, &t); if (t == s) bad++; }
    }
    KEQ(bad, 0);
    /* slots past the live table are filled too: a stray ApiNumber lands on a harmless stub, not NULL */
    for (unsigned i = r2_nlive; i < R2_NSLOT; i++) K(r2_orig[i] == NULL);
    return 0;
}

R2_SCN(hook_chain_keeps_every_window_operation_working) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "chain", 0), "cannot create a worker window");
    HWND h = k_make_window("r2cb-chain");
    R2_NEED(h, "cannot create window");
    K(IsWindow(h));
    LRESULT r = 0;
    K(r2_send(w.hwnd, WM_USER + 5, 11, 22, &r));
    KEQ(r, R2_WK_RET(WM_USER + 5));          /* the original thunk ran, the window procedure ran, the value came back */
    KEQ(w.nuser, 1);
    KEQ(w.last_wp, 11);
    KEQ(w.last_lp, 22);
    K(r2_ev_n > 0);
    K(SetWindowTextW(h, L"chained"));
    WCHAR buf[32] = L"";
    K(GetWindowTextW(h, buf, 32) == 7);
    K(!wcscmp(buf, L"chained"));
    K(DestroyWindow(h));
    K(!IsWindow(h));
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(hook_dispatcher_passes_apinumber_in_r8) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "r8", 0), "no worker");
    workload(&w);
    r2_wk_stop(&w);
    R2_NEED(r2_ev_n > 4, "workload produced no callbacks");
    LONG n = r2_ev_n < R2_NEV ? r2_ev_n : R2_NEV;
    unsigned wrong = 0, tbl_ok = 0, distinct_slots = 0;
    unsigned char seen[R2_NSLOT] = { 0 };
    for (LONG i = 0; i < n; i++) {
        struct r2_ev *e = &r2_ev_log[i];
        /* the dispatcher leaves the ApiNumber in r8 while the thunk runs */
        if ((ULONG)e->r8 != e->idx) wrong++;
        if (e->r9 == (ULONG_PTR)r2_active_table()) tbl_ok++;
        if (e->idx < R2_NSLOT && !seen[e->idx]) { seen[e->idx] = 1; distinct_slots++; }
    }
    KWEQ(wrong, 0);
    KNOTE("%ld events, %u distinct slots, r9 == table in %u of them", (long)n, distinct_slots, tbl_ok);
    KW(distinct_slots >= 3);
    return 0;
}

R2_SCN(hook_event_sequence_is_dense_and_ordered) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "seq", 0), "no worker");
    workload(&w);
    r2_wk_stop(&w);
    LONG n = r2_ev_n < R2_NEV ? r2_ev_n : R2_NEV;
    R2_NEED(n > 4, "no callbacks");
    for (LONG i = 0; i < n; i++) {
        struct r2_ev *e = &r2_ev_log[i];
        KF(e->seq == i, "event %ld has seq %ld", (long)i, (long)e->seq);
        K(e->tid != 0);
        K(e->idx < R2_NSLOT);
        K(e->ncopy <= sizeof e->data);
    }
    /* per-slot hit counters agree with the log */
    unsigned long sum = 0; for (unsigned i = 0; i < R2_NSLOT; i++) sum += (unsigned long)r2_hit[i];
    KEQ(sum, (unsigned long)r2_ev_n);
    return 0;
}

R2_SCN(hook_padding_slots_are_never_called_by_the_kernel) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "pad", 0), "no worker");
    workload(&w);
    r2_wk_stop(&w);
    R2_NEED(r2_ev_n > 4, "no callbacks");
    /* ApiNumbers beyond 0x80 do not exist: win32kfull never asks for them */
    for (unsigned i = r2_nlive; i < R2_NSLOT; i++) KF(r2_hit[i] == 0, "padding slot %u called %ld times", i, (long)r2_hit[i]);
    return 0;
}

R2_SCN(hook_uninstall_stops_recording_and_restores_dispatch) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "uninst", 0), "no worker");
    LRESULT r = 0;
    r2_send(w.hwnd, WM_USER + 9, 0, 0, &r);
    LONG during = r2_ev_n;
    K(during > 0);
    r2_uninstall();
    K(k_cb_table() == r2_saved_table());
    LONG at_uninstall = r2_ev_n;
    r = 0;
    K(r2_send(w.hwnd, WM_USER + 10, 0, 0, &r));      /* still delivered, through the original table */
    KEQ(r, R2_WK_RET(WM_USER + 10));
    KEQ(w.nuser, 2);
    KEQ(r2_ev_n, at_uninstall);                      /* ... and nobody recorded it */
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(hook_reinstall_after_uninstall) {
    NEED_HOOK();
    r2_uninstall();
    K(!r2_installed());
    K(r2_install());
    K(r2_installed());
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "reinst", 0), "no worker");
    LONG m = r2_mark();
    LRESULT r = 0;
    K(r2_send(w.hwnd, WM_USER + 11, 5, 6, &r));
    KEQ(r, R2_WK_RET(WM_USER + 11));
    KW(r2_since(2, m) >= 1);
    r2_wk_stop(&w);
    return 0;
}

static DWORD WINAPI read_table_thread(LPVOID p) { *(void **)p = k_cb_table(); return 0; }
R2_SCN(hook_patched_table_is_process_wide) {
    NEED_HOOK();
    void *seen = NULL;
    HANDLE t = CreateThread(NULL, 0, read_table_thread, &seen, 0, NULL);
    R2_NEED(t, "no thread");
    WaitForSingleObject(t, 5000);
    CloseHandle(t);
    K(seen == r2_active_table());
    /* a brand-new GUI thread (first win32k call there runs ClientThreadSetup) goes through the same table */
    struct r2_wk w; memset(&w, 0, sizeof w);
    LONG m = r2_mark();
    R2_NEED(r2_wk_start(&w, "wide", 0), "no worker");
    struct r2_ev *e = r2_find(84, m, 0);
    KW(e != NULL);
    if (e) K(e->tid == w.tid);
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(hook_original_entries_still_land_in_user32) {
    NEED_HOOK();
    ULONG_PTR lo, hi;
    R2_NEED(r2_user32_text(&lo, &hi), "no user32 text range");
    if (!r2_family()) { KNOTE("not a Windows 10 user32: placement only checked for non-NULL"); }
    for (unsigned i = 0; i < r2_nlive; i++) {
        K(r2_orig[i] != NULL);
        if (r2_family()) KF((ULONG_PTR)r2_orig[i] >= lo && (ULONG_PTR)r2_orig[i] < hi, "slot %u original %p outside user32 text", i, r2_orig[i]);
    }
    return 0;
}
