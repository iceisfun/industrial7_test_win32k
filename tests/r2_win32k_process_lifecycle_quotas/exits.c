/* exits: what win32k tears down when a thread exits (thread-owned USER objects) versus when the process exits
 * (everything), and how that is visible from outside.
 *  - USER windows (type flags 0x239), hooks (0x101) and timers are thread owned and freed in the thread-termination
 *    callout; menus and accelerator tables are process owned; GDI objects are always process owned.
 *  - The GDI shared handle table and the USER handle table are session-wide sections, so a parent can read the cells
 *    and entries of a child's handles while it lives and sees them freed after it has exited.
 */
#include "r2.h"

static int wait_user(DWORD want, DWORD ms) {
    DWORD t0 = GetTickCount();
    do { if (r2_gur(1) == want) return 1; Sleep(10); } while (GetTickCount() - t0 < ms);
    return r2_gur(1) == want;
}
static int wait_gdi(DWORD want, DWORD ms) {
    DWORD t0 = GetTickCount();
    do { if (r2_gur(0) == want) return 1; Sleep(10); } while (GetTickCount() - t0 < ms);
    return r2_gur(0) == want;
}

struct tctx { HANDLE up, go; HWND w[8]; HHOOK hk[4]; HBRUSH br[16]; UINT_PTR tm[4]; int nw, nh, nb, nt, listener, post_to; HWND target; };

static LRESULT CALLBACK hookproc(int c, WPARAM w, LPARAM l) { return U(CallNextHookEx)(NULL, c, w, l); }
static DWORD WINAPI tworker(LPVOID p) {
    struct tctx *x = p;
    for (int i = 0; i < x->nw; i++) x->w[i] = r2_window();
    for (int i = 0; i < x->nh; i++) x->hk[i] = U(SetWindowsHookExW)(WH_GETMESSAGE, hookproc, NULL, GetCurrentThreadId());
    for (int i = 0; i < x->nb; i++) x->br[i] = (HBRUSH)r2_mkgdi();   /* a bitmap, see r2_mkgdi */
    for (int i = 0; i < x->nt; i++) x->tm[i] = U(SetTimer)(NULL, 0, 60000, NULL);
    if (x->listener && x->w[0]) U(AddClipboardFormatListener)(x->w[0]);
    if (x->post_to) U(PostMessageW)(x->target, WM_USER + 7, 0x11, 0x22);
    SetEvent(x->up);
    WaitForSingleObject(x->go, 10000);
    return 0;
}
static HANDLE g_th;
static int run_worker(struct tctx *x, DWORD *base_u, DWORD *base_g) {
    x->up = CreateEventA(NULL, TRUE, FALSE, NULL); x->go = CreateEventA(NULL, TRUE, FALSE, NULL);
    *base_u = r2_gur(1); *base_g = r2_gur(0);
    g_th = CreateThread(NULL, 0, tworker, x, 0, NULL);
    if (!g_th) return 0;
    if (WaitForSingleObject(x->up, 10000) != WAIT_OBJECT_0) { SetEvent(x->go); r2_wait_thread(g_th, 5000); CloseHandle(g_th); g_th = NULL; return 0; }
    return 1;
}
static void finish_worker(struct tctx *x) {
    SetEvent(x->go);
    r2_wait_thread(g_th, 10000);
    CloseHandle(g_th); g_th = NULL;
    CloseHandle(x->up); CloseHandle(x->go);
}

R2_SCN(exits_thread_windows_destroyed_and_counted) {
    R2_CHILD_NEED_WIN();
    struct tctx x; memset(&x, 0, sizeof x); x.nw = 5;
    DWORD bu, bg;
    RC(run_worker(&x, &bu, &bg)); if (!x.up) return;
    /* the process count includes other threads' windows (a thread's first window may add a default IME window) */
    RCF(r2_gur(1) >= bu + 5, "USER %lu base %lu", r2_gur(1), bu);
    for (int i = 0; i < 5; i++) RC(U(IsWindow)(x.w[i]));
    finish_worker(&x);
    RC(wait_user(bu, 3000));
    for (int i = 0; i < 5; i++) RCF(!U(IsWindow)(x.w[i]), "window %d survived its thread", i);
    REQ(r2_gur(0), bg);
}
R2_SCN(exits_thread_dead_window_calls_fail_1400) {
    R2_CHILD_NEED_WIN();
    struct tctx x; memset(&x, 0, sizeof x); x.nw = 1;
    DWORD bu, bg;
    RC(run_worker(&x, &bu, &bg)); if (!x.up) return;
    HWND w = x.w[0];
    finish_worker(&x);
    RC(wait_user(bu, 3000));
    SetLastError(0);
    RC(U(SendMessageTimeoutW)(w, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 200, NULL) == 0);
    RC(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    SetLastError(0);
    RC(U(DestroyWindow)(w) == FALSE);
    RC(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    DWORD pid = 0; SetLastError(0);
    RC(U(GetWindowThreadProcessId)(w, &pid) == 0 && pid == 0);
}
R2_SCN(exits_thread_hooks_removed) {
    R2_CHILD_NEED_WIN();
    struct tctx x; memset(&x, 0, sizeof x); x.nh = 3;
    DWORD bu, bg;
    RC(run_worker(&x, &bu, &bg)); if (!x.up) return;
    for (int i = 0; i < 3; i++) RC(x.hk[i] != NULL);
    REQ(r2_gur(1), bu + 3 + 1);                          /* three hooks and the new GUI thread's default IME window */
    HHOOK h0 = x.hk[0];
    finish_worker(&x);
    RC(wait_user(bu, 3000));
    SetLastError(0);
    RC(U(UnhookWindowsHookEx)(h0) == FALSE);
    RC(GetLastError() == ERROR_INVALID_HOOK_HANDLE);
}
R2_SCN(exits_thread_timers_freed) {
    R2_CHILD_NEED_WIN();
    struct tctx x; memset(&x, 0, sizeof x); x.nt = 3;
    DWORD bu, bg;
    RC(run_worker(&x, &bu, &bg)); if (!x.up) return;
    for (int i = 0; i < 3; i++) RC(x.tm[i] != 0);
    t_note("USER count with 3 thread timers: +%ld", (long)r2_gur(1) - (long)bu);
    finish_worker(&x);
    RC(wait_user(bu, 3000));                             /* thread timers die with their thread */
}
R2_SCN(exits_thread_clipboard_listener_window_gone) {
    R2_CHILD_NEED_WIN();
    struct tctx x; memset(&x, 0, sizeof x); x.nw = 1; x.listener = 1;
    DWORD bu, bg;
    RC(run_worker(&x, &bu, &bg)); if (!x.up) return;
    HWND w = x.w[0];
    finish_worker(&x);
    RC(wait_user(bu, 3000));
    SetLastError(0);
    RC(U(RemoveClipboardFormatListener)(w) == FALSE);    /* the window is gone, and with it the listener registration */
    RC(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
}
R2_SCN(exits_thread_gdi_objects_survive) {
    R2_CHILD_NEED_WIN();
    struct tctx x; memset(&x, 0, sizeof x); x.nb = 12;
    DWORD bu, bg;
    RC(run_worker(&x, &bu, &bg)); if (!x.up) return;
    REQ(r2_gur(0), bg + 12);
    finish_worker(&x);
    Sleep(100);
    REQ(r2_gur(0), bg + 12);                             /* GDI objects belong to the process, not the thread */
    for (int i = 0; i < 12; i++) { RCF(U(GetObjectType)(x.br[i]) == OBJ_BITMAP, "object %d", i); RC(U(DeleteObject)(x.br[i])); }
    RC(wait_gdi(bg, 2000));
}
R2_SCN(exits_thread_gdi_cell_alive_after_thread_exit) {
    R2_CHILD_NEED_WIN();
    struct tctx x; memset(&x, 0, sizeof x); x.nb = 1;
    DWORD bu, bg;
    RC(run_worker(&x, &bu, &bg)); if (!x.up) return;
    HBRUSH b = x.br[0];
    struct k_gdicell c0, c1;
    RC(k_gdi_cell(b, &c0));
    finish_worker(&x);
    RC(k_gdi_cell(b, &c1));
    RC(c0.object == c1.object); RC(c0.type_check == c1.type_check);
    RC(c1.type_check == (unsigned char)(((ULONG_PTR)b >> 24) & 0xFF));
    RC(U(DeleteObject)(b));
}
R2_SCN(exits_thread_posted_message_outlives_sender_thread) {
    R2_CHILD_NEED_WIN();
    HWND me = r2_window(); RC(me != NULL); if (!me) return;
    struct tctx x; memset(&x, 0, sizeof x); x.post_to = 1; x.target = me;
    DWORD bu, bg;
    RC(run_worker(&x, &bu, &bg)); if (!x.up) return;
    finish_worker(&x);
    MSG m; int got = 0;
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 2000 && !got) {
        if (U(PeekMessageW)(&m, me, WM_USER + 7, WM_USER + 7, PM_REMOVE)) got = 1; else Sleep(5);
    }
    RC(got);
    if (got) { RC(m.wParam == 0x11 && m.lParam == 0x22); }
    U(DestroyWindow)(me);
}
R2_SCN(exits_thread_generations_return_to_baseline) {
    R2_CHILD_NEED_WIN();
    DWORD bu = r2_gur(1), bg = r2_gur(0);
    for (int i = 0; i < 40; i++) {
        struct tctx x; memset(&x, 0, sizeof x); x.nw = 2; x.nh = 1; x.nt = 1;
        DWORD u, g;
        if (!run_worker(&x, &u, &g)) { RC(0); break; }
        finish_worker(&x);
    }
    RC(wait_user(bu, 4000));
    REQ(r2_gur(0), bg);
}
R2_SCN(exits_thread_peak_survives_thread_exit) {
    R2_CHILD_NEED_WIN();
    struct tctx x; memset(&x, 0, sizeof x); x.nw = 6;
    DWORD bu, bg;
    RC(run_worker(&x, &bu, &bg)); if (!x.up) return;
    DWORD peak_live = r2_gur(GR_USEROBJECTS_PEAK_);
    RC(peak_live >= bu + 6);
    finish_worker(&x);
    RC(wait_user(bu, 3000));
    RC(r2_gur(GR_USEROBJECTS_PEAK_) >= peak_live);       /* the high-water mark never moves down */
}
R2_SCN(exits_thread_pti_pointer_cleared_in_dead_thread_only) {
    R2_CHILD_NEED_WIN();
    void *mine = NULL; k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &mine);
    struct tctx x; memset(&x, 0, sizeof x); x.nw = 1;
    DWORD bu, bg;
    RC(run_worker(&x, &bu, &bg)); if (!x.up) return;
    finish_worker(&x);
    wait_user(bu, 3000);
    void *again = NULL; k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &again);
    RC(mine && mine == again);                           /* the main thread's THREADINFO is unaffected */
    HWND w = r2_window(); RC(w && U(IsWindow)(w)); if (w) U(DestroyWindow)(w);
}
R2_SCN(exits_gdi_cell_stale_after_delete) {
    R2_CHILD_NEED_WIN();
    HBRUSH b = (HBRUSH)r2_mkgdi(); RC(b != NULL); if (!b) return;
    struct k_gdicell c0, c1; RC(k_gdi_cell(b, &c0));
    RC(U(DeleteObject)(b));
    RC(k_gdi_cell(b, &c1));
    /* the handle no longer validates: the stored uniqueness byte moved, the type changed or the kernel pointer went */
    RCF(c1.type_check != c0.type_check || c1.object != c0.object || c1.field10 != c0.field10, "cell unchanged after DeleteObject");
    SetLastError(0);
    RC(U(DeleteObject)(b) == FALSE);
    /* Observed on Windows 10 22H2: GetObjectType keeps answering the type held in the handle value, so the deleted
     * bitmap still reports OBJ_BITMAP; the second DeleteObject above is what shows the handle is dead. */
    RC(U(GetObjectType)(b) == OBJ_BITMAP);
}

/* ---------------- process exit, observed by the parent ---------------- */

/* Child: build up GDI/USER objects in phases, parking between them. */
R2_SCN(proc_phases) {
    R2_CHILD_NEED_WIN();
    /* the thread's first window brings a default IME window with it, which goes away with the thread's last window:
     * keep one window alive so that the later windows add exactly one USER object each */
    HWND warm = r2_window(); RC(warm != NULL);
    DWORD g0 = r2_gur(0), u0 = r2_gur(1);
    r2_publish("g0", g0); r2_publish("u0", u0);
    HBRUSH br[25]; HMENU mn[5]; HWND w[3];
    for (int i = 0; i < 25; i++) br[i] = (HBRUSH)r2_mkgdi();
    for (int i = 0; i < 5; i++) mn[i] = U(CreateMenu)();
    for (int i = 0; i < 3; i++) w[i] = r2_window();
    r2_publish("hwnd", (ULONGLONG)(ULONG_PTR)w[0]);
    r2_publish("hbr", (ULONGLONG)(ULONG_PTR)br[0]);
    r2_publish("gcur", r2_gur(0)); r2_publish("ucur", r2_gur(1));
    r2_publish("gpeak", r2_gur(2)); r2_publish("upeak", r2_gur(4));
    r2_park();                                           /* parent inspects the full state */
    for (int i = 0; i < 25; i++) U(DeleteObject)(br[i]);
    for (int i = 0; i < 5; i++) U(DestroyMenu)(mn[i]);
    for (int i = 0; i < 3; i++) U(DestroyWindow)(w[i]);
    r2_publish("gafter", r2_gur(0)); r2_publish("uafter", r2_gur(1));
    r2_park();                                           /* parent inspects the torn-down state */
    /* leave a few objects behind for the exit cleanup */
    HBRUSH leak = (HBRUSH)r2_mkgdi(); HWND lw = r2_window();
    r2_publish("hleak", (ULONGLONG)(ULONG_PTR)leak); r2_publish("hlw", (ULONGLONG)(ULONG_PTR)lw);
    r2_park();
}
static DWORD hget(struct r2_proc *p, const char *k) { ULONGLONG v = 0xFFFFFFFFFFFFFFFFULL; r2_fetch(p, k, &v); return (DWORD)v; }

TEST(r2_win32k_process_lifecycle_quotas_proc_counts_seen_from_parent_through_phases) {
    R2_NEED_WIN();
    struct r2_proc p;
    if (!r2_start(&p, "proc_phases")) { t_skip("cannot start child"); return; }
    if (!r2_wait_ready(&p, 20000)) { CHECK(0); r2_finish(&p, 100); return; }
    DWORD g0 = hget(&p, "g0"), u0 = hget(&p, "u0");
    HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, p.pid);
    REQUIRE(hp);
    DWORD g = U(GetGuiResources)(hp, 0), u = U(GetGuiResources)(hp, 1);
    CHECKF(g == g0 + 25, "child GDI %lu base %lu", g, g0);
    CHECKF(u == u0 + 8, "child USER %lu base %lu", u, u0);
    CHECK(U(GetGuiResources)(hp, 2) >= g);
    CHECK(U(GetGuiResources)(hp, 4) >= u);
    CHECK(U(GetGuiResources)(hp, 2) == hget(&p, "gpeak"));
    /* the same counters seen from the child's own side agree */
    CHECK(g == hget(&p, "gcur") && u == hget(&p, "ucur"));
    /* the parent's own counters are not affected by the child's objects */
    DWORD my_g = r2_gur(0);
    r2_go(&p);
    CHECK(r2_wait_ready(&p, 20000));
    DWORD gpk = U(GetGuiResources)(hp, 2), upk = U(GetGuiResources)(hp, 4);
    CHECK(U(GetGuiResources)(hp, 0) == g0); CHECK(U(GetGuiResources)(hp, 1) == u0);
    CHECKF(gpk >= g0 + 25 && upk >= u0 + 8, "peaks gdi %lu user %lu", gpk, upk);
    CHECK(gpk == hget(&p, "gpeak") && upk == hget(&p, "upeak"));   /* nothing raised the peak while objects were freed */
    CHECK(my_g == r2_gur(0));
    r2_go(&p);
    CHECK(r2_wait_ready(&p, 20000));
    /* leaked objects: handle table cells/entries belong to the child while it lives */
    ULONGLONG leak = 0, lw = 0;
    CHECK(r2_fetch(&p, "hleak", &leak) && r2_fetch(&p, "hlw", &lw));
    struct k_gdicell c; CHECK(k_gdi_cell((void *)(ULONG_PTR)leak, &c));
    CHECK(c.object != NULL);
    CHECK(c.type_check == ((leak >> 24) & 0xFF));
    struct k_user_entry e; CHECK(k_user_entry((HANDLE)(ULONG_PTR)lw, &e)); CHECK(e.type == K_TYPE_WINDOW);
    CHECK(U(IsWindow)((HWND)(ULONG_PTR)lw));
    DWORD wpid = 0; U(GetWindowThreadProcessId)((HWND)(ULONG_PTR)lw, &wpid);
    CHECK(wpid == p.pid);
    r2_go(&p);
    CHECK(r2_finish(&p, 20000) == 0);
    /* after process exit: every object is gone */
    DWORD t0 = GetTickCount();
    while (U(IsWindow)((HWND)(ULONG_PTR)lw) && GetTickCount() - t0 < 3000) Sleep(10);
    CHECK(!U(IsWindow)((HWND)(ULONG_PTR)lw));
    CHECK(k_user_entry((HANDLE)(ULONG_PTR)lw, &e));
    CHECKF(e.type != K_TYPE_WINDOW || (e.flags & 1) || e.uniq != (((ULONG_PTR)lw >> 16) & 0xFFFF), "USER entry still describes the window");
    struct k_gdicell c2; CHECK(k_gdi_cell((void *)(ULONG_PTR)leak, &c2));
    CHECKF(c2.type_check != c.type_check || c2.object != c.object || c2.field10 != c.field10, "GDI cell still describes the brush");
    /* As above, the dead process's bitmap handle still reports
     * OBJ_BITMAP; the freed cell checked above is what shows it is gone */
    /* Whether the dead process's slot has been reused by another allocation by now depends on what else runs on the
     * machine: the handle reports OBJ_BITMAP while the cell is untouched and 0 once it was recycled; nothing else. */
    { DWORD ot = U(GetObjectType)((HGDIOBJ)(ULONG_PTR)leak); CHECKF(ot == OBJ_BITMAP || ot == 0, "type %lu", ot); }
    CHECK(U(GetGuiResources)(hp, 0) == 0); CHECK(U(GetGuiResources)(hp, 1) == 0);
    CloseHandle(hp);
}

R2_SCN(proc_window_owner) {
    R2_CHILD_NEED_WIN();
    HWND w = r2_window();
    r2_publish("w", (ULONGLONG)(ULONG_PTR)w);
    r2_publish("tid", GetCurrentThreadId());
    r2_park();
}
TEST(r2_win32k_process_lifecycle_quotas_proc_foreign_window_rules_and_death) {
    R2_NEED_WIN();
    struct r2_proc p;
    if (!r2_start(&p, "proc_window_owner")) { t_skip("cannot start child"); return; }
    if (!r2_wait_ready(&p, 20000)) { CHECK(0); r2_finish(&p, 100); return; }
    ULONGLONG wv = 0, tid = 0; CHECK(r2_fetch(&p, "w", &wv)); CHECK(r2_fetch(&p, "tid", &tid));
    HWND w = (HWND)(ULONG_PTR)wv;
    CHECK(U(IsWindow)(w));
    DWORD pid = 0; DWORD t = U(GetWindowThreadProcessId)(w, &pid);
    CHECK(pid == p.pid); CHECK(t == (DWORD)tid);
    SetLastError(0);
    CHECK(U(DestroyWindow)(w) == FALSE);                 /* only the owning thread may destroy a window */
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    CHECK(U(IsWindow)(w));
    r2_go(&p);
    CHECK(r2_finish(&p, 20000) == 0);
    DWORD t0 = GetTickCount();
    while (U(IsWindow)(w) && GetTickCount() - t0 < 3000) Sleep(10);
    CHECK(!U(IsWindow)(w));
    SetLastError(0);
    CHECK(U(GetWindowThreadProcessId)(w, &pid) == 0);
}
