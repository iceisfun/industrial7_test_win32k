/* Handles of another process and of another desktop: the USER handle table is shared by every
 * process of the session, so entries of a peer are visible here; ownership and desktop rules decide
 * what a foreign handle can be used for. */
#include "r2h.h"

UDECL(ULONG_PTR, NtUserQueryWindow, (HWND, DWORD));

/* ------------------------------------------------------------------ peer process */
CHILD(r2h_peer) {
    /* argv[1] = parent pid, argv[2] = result file */
    char nm[96];
    snprintf(nm, sizeof nm, "Local\\win32kt_r2h_ready_%s", argv[1]);
    HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, nm);
    snprintf(nm, sizeof nm, "Local\\win32kt_r2h_done_%s", argv[1]);
    HANDLE done = OpenEventA(SYNCHRONIZE, FALSE, nm);
    if (!ready || !done) return 2;
    if (!U(GetDesktopWindow)) return 3;
    U(GetDesktopWindow)();
    HWND w = r2h_make_top();
    HMENU m = U(CreateMenu)();
    HANDLE acc = r2h_d_accel.create();
    HANDLE ico = r2h_d_icon.create();
    char buf[256];
    snprintf(buf, sizeof buf, "%llx %llx %llx %llx %lu %lu", (unsigned long long)(ULONG_PTR)w, (unsigned long long)(ULONG_PTR)m,
             (unsigned long long)(ULONG_PTR)acc, (unsigned long long)(ULONG_PTR)ico, GetCurrentThreadId(), U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS));
    HANDLE f = CreateFileA(argv[2], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 4;
    DWORD wr; WriteFile(f, buf, (DWORD)strlen(buf), &wr, NULL); CloseHandle(f);
    SetEvent(ready);
    DWORD t0 = GetTickCount();
    for (;;) {
        MSG msg;
        while (U(PeekMessageW)(&msg, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&msg); U(DispatchMessageW)(&msg); }
        if (WaitForSingleObject(done, 0) == WAIT_OBJECT_0) break;
        if (GetTickCount() - t0 > 20000) break;
        U(MsgWaitForMultipleObjects)(1, &done, FALSE, 100, QS_ALLINPUT);
    }
    return 0;
}
struct peer { PROCESS_INFORMATION pi; HANDLE ready, done, proc; char file[MAX_PATH];
              HWND w; HMENU m; HANDLE acc, ico; DWORD tid, gui; };
static int peer_start(struct peer *p) {
    char nm[96], extra[MAX_PATH + 64], txt[256] = "";
    memset(p, 0, sizeof *p);
    snprintf(nm, sizeof nm, "Local\\win32kt_r2h_ready_%lu", GetCurrentProcessId());
    p->ready = CreateEventA(NULL, TRUE, FALSE, nm);
    snprintf(nm, sizeof nm, "Local\\win32kt_r2h_done_%lu", GetCurrentProcessId());
    p->done = CreateEventA(NULL, TRUE, FALSE, nm);
    snprintf(p->file, sizeof p->file, "%sr2h_peer.txt", t_tmpdir());
    snprintf(extra, sizeof extra, "%lu \"%s\"", GetCurrentProcessId(), p->file);
    p->proc = t_spawn_child("r2h_peer", extra, 0, NULL, NULL, NULL, FALSE, NULL, &p->pi);
    if (!p->proc) return 0;
    if (WaitForSingleObject(p->ready, 15000) != WAIT_OBJECT_0) return 0;
    HANDLE f = CreateFileA(p->file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD n = 0; ReadFile(f, txt, sizeof txt - 1, &n, NULL); txt[n] = 0; CloseHandle(f);
    unsigned long long a, b, c, d; unsigned long tid, gui;
    if (sscanf(txt, "%llx %llx %llx %llx %lu %lu", &a, &b, &c, &d, &tid, &gui) != 6) return 0;
    p->w = (HWND)(ULONG_PTR)a; p->m = (HMENU)(ULONG_PTR)b; p->acc = (HANDLE)(ULONG_PTR)c; p->ico = (HANDLE)(ULONG_PTR)d;
    p->tid = tid; p->gui = gui;
    return p->w != NULL;
}
static void peer_stop(struct peer *p) {
    if (p->done) SetEvent(p->done);
    if (p->proc) { if (WaitForSingleObject(p->proc, 8000) != WAIT_OBJECT_0) TerminateProcess(p->proc, 1); CloseHandle(p->pi.hThread); CloseHandle(p->proc); }
    if (p->ready) CloseHandle(p->ready);
    if (p->done) CloseHandle(p->done);
}
#define PEER_NEED(p) struct peer p; if (!peer_start(&p)) { peer_stop(&p); t_skip("peer process did not start"); return; }

R2(peer_window_entry_owned_by_peer_thread) {
    R2H_NEED(); PEER_NEED(p);
    r2h_entry_t e;
    CHECK(r2h_read_h(p.w, &e));
    EXPECT_EQ(e.type, UT_WINDOW);
    EXPECT_EQ(e.uniq, R2H_UNIQ(p.w));
    EXPECT_EQ(e.owner, p.tid);
    CHECK((e.flags & 1) == 0);
    CHECK(e.delta != 0);
    CHECK(U(IsWindow)(p.w));
    peer_stop(&p);
}
R2(peer_window_owner_queries) {
    R2H_NEED(); PEER_NEED(p);
    DWORD pid = 0;
    EXPECT_EQ(U(GetWindowThreadProcessId)(p.w, &pid), p.tid);
    EXPECT_EQ(pid, p.pi.dwProcessId);
    if (UN(NtUserQueryWindow)) {
        EXPECT_EQ(UN(NtUserQueryWindow)(p.w, 0), p.pi.dwProcessId);
        EXPECT_EQ(UN(NtUserQueryWindow)(p.w, 2), p.tid);
    }
    peer_stop(&p);
}
/* Owner thread != caller: window destruction refuses with ERROR_ACCESS_DENIED and the entry stays live. */
R2(peer_window_destroy_denied) {
    R2H_NEED(); PEER_NEED(p);
    r2h_entry_t e0, e1;
    CHECK(r2h_read_h(p.w, &e0));
    BOOL r; R2H_CALL_ERR(r, U(DestroyWindow)(p.w));
    CHECK(!r);
    EXPECT_ERR(5);
    CHECK(r2h_read_h(p.w, &e1));
    EXPECT_EQ(e1.type, UT_WINDOW); EXPECT_EQ(e1.uniq, e0.uniq); CHECK((e1.flags & 1) == 0);
    CHECK(U(IsWindow)(p.w));
    peer_stop(&p);
}
/* Windows of one desktop share one desktop heap mapping, so the peer's window head is readable here. */
R2(peer_window_head_readable_through_heap_mapping) {
    R2H_NEED(); PEER_NEED(p);
    void *pw = k_wnd_ptr(p.w);
    if (!pw) { peer_stop(&p); t_skip("fast-path heap view not resolvable"); return; }
    unsigned long long q[2] = {0, 0};
    CHECK(k_try_read(pw, q, sizeof q));
    EXPECT_EQ((unsigned)q[0], (unsigned)(ULONG_PTR)p.w);
    r2h_entry_t e; CHECK(r2h_read_h(p.w, &e));
    EXPECT_EQ(q[1], e.delta);
    peer_stop(&p);
}
/* Entries of the peer's process-owned objects carry the peer pid, not ours. */
R2(peer_process_owned_entries_carry_peer_pid) {
    R2H_NEED(); PEER_NEED(p);
    r2h_entry_t em, ea, ei;
    CHECK(r2h_read_h(p.m, &em));
    CHECK(r2h_read_h(p.acc, &ea));
    CHECK(r2h_read_h(p.ico, &ei));
    EXPECT_EQ(em.type, UT_MENU);   EXPECT_EQ(em.owner, p.pi.dwProcessId);
    EXPECT_EQ(ea.type, UT_ACCEL);  EXPECT_EQ(ea.owner, p.pi.dwProcessId);
    EXPECT_EQ(ei.type, UT_CURSOR); EXPECT_EQ(ei.owner, p.pi.dwProcessId);
    EXPECT_EQ(em.uniq, R2H_UNIQ(p.m)); EXPECT_EQ(ea.uniq, R2H_UNIQ(p.acc)); EXPECT_EQ(ei.uniq, R2H_UNIQ(p.ico));
    peer_stop(&p);
}
/* IsMenu only compares the entry type and uniqueness: the peer's menu validates here. */
R2(peer_menu_validates_in_other_process) {
    R2H_NEED(); PEER_NEED(p);
    CHECK(U(IsMenu)(p.m));
    peer_stop(&p);
}
/* The peer's entries are indexes into the very same table: distinct from every entry we own. */
R2(peer_indices_distinct_from_own) {
    R2H_NEED(); PEER_NEED(p);
    HWND w = r2h_make_top(); HMENU m = U(CreateMenu)();
    REQUIRE(w); REQUIRE(m);
    CHECK(R2H_IDX(w) != R2H_IDX(p.w));
    CHECK(R2H_IDX(m) != R2H_IDX(p.m));
    CHECK(R2H_IDX(m) != R2H_IDX(p.acc));
    CHECK(R2H_IDX(m) != R2H_IDX(p.ico));
    U(DestroyMenu)(m); U(DestroyWindow)(w);
    peer_stop(&p);
}
static DWORD s_ptids[256]; static int s_nptids;
static void peer_load_tids(DWORD pid) {
    s_nptids = 0;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (s == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te; te.dwSize = sizeof te;
    if (Thread32First(s, &te)) do { if (te.th32OwnerProcessID == pid && s_nptids < 256) s_ptids[s_nptids++] = te.th32ThreadID; } while (Thread32Next(s, &te));
    CloseHandle(s);
}
static int peer_has_tid(DWORD pid, DWORD tid) {
    for (int i = 0; i < s_nptids; i++) if (s_ptids[i] == tid) return 1;
    return 0;
}
/* GetGuiResources(peer) = the peer's per-process handle counter: every counted-type entry owned by the
 * peer process or its threads. */
R2(peer_gui_resources_equal_owned_entry_count) {
    R2H_NEED(); PEER_NEED(p);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pi.dwProcessId);
    if (!h) { peer_stop(&p); t_skip("OpenProcess failed"); return; }
    DWORD gr = U(GetGuiResources)(h, GR_USEROBJECTS);
    CHECK(gr >= p.gui);   /* sampled before the peer parked itself */
    peer_load_tids(p.pi.dwProcessId);
    unsigned n; r2h_entry_t *t = r2h_snap(&n); unsigned cnt = 0;
    for (unsigned i = 0; t && i < n; i++) {
        if (!t[i].type || !r2h_counted_type(t[i].type)) continue;
        if (t[i].owner == p.pi.dwProcessId || peer_has_tid(p.pi.dwProcessId, (DWORD)t[i].owner)) cnt++;
    }
    free(t);
    EXPECT_EQ(cnt, gr);
    CloseHandle(h);
    peer_stop(&p);
}
/* Process exit reclaims everything: the handles go stale and validate as invalid here. */
R2(peer_exit_invalidates_foreign_handles) {
    R2H_NEED(); PEER_NEED(p);
    HWND w = p.w; HMENU m = p.m; unsigned wi = R2H_IDX(w), wu = R2H_UNIQ(w);
    CHECK(U(IsWindow)(w));
    peer_stop(&p);
    int gone = 0;
    for (int i = 0; i < 100; i++) { if (!U(IsWindow)(w) && !U(IsMenu)(m)) { gone = 1; break; } Sleep(50); }
    CHECK(gone);
    r2h_entry_t e;
    CHECK(r2h_read(wi, &e));
    CHECK(!(e.type == UT_WINDOW && e.uniq == wu && !(e.flags & 1)));
    DWORD pid = 5;
    SetLastError(R2H_SENTINEL);
    EXPECT_EQ(U(GetWindowThreadProcessId)(w, &pid), 0);
    EXPECT_ERR(0x578);
}

/* ------------------------------------------------------------------ other desktop */
static HDESK mk_desktop(void) {
    WCHAR nm[64]; wsprintfW(nm, L"win32kt_r2h_dsk_%lu_%lu", GetCurrentProcessId(), GetTickCount());
    return U(CreateDesktopW)(nm, NULL, NULL, 0, GENERIC_ALL, NULL);
}
/* Each desktop has its own heap: the guard word stored in desktop-heap entries (f10 =
 * pDesktopInfo->pvDesktopBase) differs between a window of this desktop and one of a new desktop. */
R2(other_desktop_window_entry_has_own_heap_base) {
    R2H_NEED();
    HDESK d = mk_desktop();
    if (!d) { t_skip("CreateDesktop failed (err %lu)", GetLastError()); return; }
    struct r2h_gw g;
    if (!r2h_gw_start(&g, d)) { r2h_gw_stop(&g); U(CloseDesktop)(d); t_skip("window on new desktop not created"); return; }
    HWND mine = r2h_make_top(); REQUIRE(mine);
    r2h_entry_t eo, em;
    CHECK(r2h_read_h(g.w, &eo));
    CHECK(r2h_read_h(mine, &em));
    EXPECT_EQ(eo.type, UT_WINDOW);
    EXPECT_EQ(eo.owner, g.tid);
    EXPECT_EQ(eo.uniq, R2H_UNIQ(g.w));
    CHECK(eo.delta != 0);
    CHECK(eo.f10 != 0);
    CHECK(eo.f10 != em.f10);
    U(DestroyWindow)(mine);
    r2h_gw_stop(&g);
    U(CloseDesktop)(d);
}
/* The owner thread itself can always use the window (window handle validation: same thread). */
R2(other_desktop_window_valid_for_its_thread) {
    R2H_NEED();
    HDESK d = mk_desktop();
    if (!d) { t_skip("CreateDesktop failed (err %lu)", GetLastError()); return; }
    struct r2h_gw g;
    if (!r2h_gw_start(&g, d)) { r2h_gw_stop(&g); U(CloseDesktop)(d); t_skip("window on new desktop not created"); return; }
    r2h_entry_t e; CHECK(r2h_read_h(g.w, &e));
    EXPECT_EQ(e.type, UT_WINDOW); CHECK((e.flags & 1) == 0);
    r2h_gw_stop(&g);
    U(CloseDesktop)(d);
}
/* From a thread on another desktop the owner queries either fail with 1400 (window handle validation
 * compares the window's desktop with the caller's desktop) or return the right ids; never wrong ids. */
R2(other_desktop_window_queries_never_lie) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HDESK d = mk_desktop();
    if (!d) { t_skip("CreateDesktop failed (err %lu)", GetLastError()); return; }
    struct r2h_gw g;
    if (!r2h_gw_start(&g, d)) { r2h_gw_stop(&g); U(CloseDesktop)(d); t_skip("window on new desktop not created"); return; }
    ULONG_PTR tid, pid;
    SetLastError(R2H_SENTINEL);
    tid = UN(NtUserQueryWindow)(g.w, 2);
    DWORD e = GetLastError();
    pid = UN(NtUserQueryWindow)(g.w, 0);
    CHECKF(tid == 0 || tid == g.tid, "tid 0x%llx", (unsigned long long)tid);
    CHECKF(pid == 0 || pid == GetCurrentProcessId(), "pid 0x%llx", (unsigned long long)pid);
    if (tid == 0) EXPECT_EQ(e, 0x578);
    t_info("cross-desktop QueryWindow: tid=%llu pid=%llu err=%lu", (unsigned long long)tid, (unsigned long long)pid, e);
    r2h_gw_stop(&g);
    U(CloseDesktop)(d);
}
/* Destroying the desktop's last window thread removes its entries from the shared table. */
R2(other_desktop_window_freed_when_thread_exits) {
    R2H_NEED();
    HDESK d = mk_desktop();
    if (!d) { t_skip("CreateDesktop failed (err %lu)", GetLastError()); return; }
    struct r2h_gw g;
    if (!r2h_gw_start(&g, d)) { r2h_gw_stop(&g); U(CloseDesktop)(d); t_skip("window on new desktop not created"); return; }
    HWND w = g.w; unsigned idx = R2H_IDX(w), uq = R2H_UNIQ(w);
    r2h_gw_stop(&g);
    int gone = 0;
    for (int i = 0; i < 100 && !gone; i++) {
        r2h_entry_t e;
        if (r2h_read(idx, &e) && !(e.type == UT_WINDOW && e.uniq == uq)) gone = 1; else Sleep(20);
    }
    CHECK(gone);
    U(CloseDesktop)(d);
}
