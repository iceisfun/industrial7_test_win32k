/* Thread ownership, cross-thread access and process-level lifetime of USER objects, plus
 * fresh-process and second-process views (children). */
#include "uh.h"

UDECL(DWORD, NtUserGetGuiResources, (HANDLE, DWORD));
UDECL(ULONG_PTR, NtUserGetThreadState, (DWORD));

struct tctx {
    HANDLE ready, go;
    DWORD tid;
    HWND w; HMENU m; HACCEL a; HHOOK hook; HDWP dwp;
    int stage;
};
static DWORD WINAPI helper(LPVOID p) {
    struct tctx *c = p;
    U(GetDesktopWindow)();
    c->tid = GetCurrentThreadId();
    c->w = uh_make_top();
    c->m = U(CreateMenu)();
    c->a = (HACCEL)uh_d_accel.create();
    c->hook = (HHOOK)uh_d_hook.create();
    c->dwp = (HDWP)uh_d_hdwp.create();
    SetEvent(c->ready);
    WaitForSingleObject(c->go, 10000);
    return 0;
}
static HANDLE start_helper(struct tctx *c) {
    memset(c, 0, sizeof *c);
    c->ready = CreateEventW(NULL, TRUE, FALSE, NULL); c->go = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, helper, c, 0, NULL);
    if (!t || WaitForSingleObject(c->ready, 8000) != WAIT_OBJECT_0) { if (t) { SetEvent(c->go); CloseHandle(t); } return NULL; }
    return t;
}
static void stop_helper(struct tctx *c, HANDLE t) {
    SetEvent(c->go);
    WaitForSingleObject(t, 5000);
    CloseHandle(t); CloseHandle(c->ready); CloseHandle(c->go);
}
#define HELPER_BEGIN() struct tctx c; UH_NEED(); HANDLE th = start_helper(&c); if (!th) { t_skip("helper thread failed"); return; }
#define HELPER_END() stop_helper(&c, th)

TEST(user_handle_table_thread_owner_ids) {
    HELPER_BEGIN();
    uh_entry_t e;
    CHECK(c.w && uh_read_h(c.w, &e)); EXPECT_EQ(e.owner, c.tid); EXPECT_EQ(e.type, UT_WINDOW);
    CHECK(c.hook && uh_read_h(c.hook, &e)); EXPECT_EQ(e.owner, c.tid); EXPECT_EQ(e.type, UT_HOOK);
    CHECK(c.dwp && uh_read_h(c.dwp, &e)); EXPECT_EQ(e.owner, c.tid); EXPECT_EQ(e.type, UT_SETWINDOWPOS);
    CHECK(c.m && uh_read_h(c.m, &e)); EXPECT_EQ(e.owner, GetCurrentProcessId()); EXPECT_EQ(e.type, UT_MENU);
    CHECK(c.a && uh_read_h(c.a, &e)); EXPECT_EQ(e.owner, GetCurrentProcessId()); EXPECT_EQ(e.type, UT_ACCEL);
    CHECK(c.tid != GetCurrentThreadId());
    HELPER_END();
}
TEST(user_handle_table_thread_window_visible_from_other_thread) {
    HELPER_BEGIN();
    CHECK(U(IsWindow)(c.w));
    DWORD pid = 0; EXPECT_EQ(U(GetWindowThreadProcessId)(c.w, &pid), c.tid); EXPECT_EQ(pid, GetCurrentProcessId());
    HWND mine = uh_make_top();
    uh_entry_t a, b;
    if (mine && uh_read_h(mine, &a) && uh_read_h(c.w, &b)) EXPECT_EQ(a.f10, b.f10); /* same desktop heap */
    if (mine) U(DestroyWindow)(mine);
    HELPER_END();
}
TEST(user_handle_table_thread_cross_thread_destroywindow_denied) {
    HELPER_BEGIN();
    BOOL r; UH_CALL_ERR(r, U(DestroyWindow)(c.w));
    CHECK(!r);
    EXPECT_ERR(5); /* DestroyWindow on a window of another thread => ERROR_ACCESS_DENIED */
    CHECK(U(IsWindow)(c.w));
    uh_entry_t e; CHECK(uh_read_h(c.w, &e)); EXPECT_EQ(e.type, UT_WINDOW);
    HELPER_END();
}
TEST(user_handle_table_thread_cross_thread_unhook_allowed) {
    HELPER_BEGIN();
    CHECK(U(UnhookWindowsHookEx)(c.hook)); /* UnhookWindowsHookEx has no owner-thread test */
    uh_entry_t e; CHECK(uh_read_h(c.hook, &e)); CHECK(e.type == 0 || (e.flags & 1));
    BOOL r; UH_CALL_ERR(r, U(UnhookWindowsHookEx)(c.hook)); CHECK(!r); EXPECT_ERR(0x57c);
    HELPER_END();
}
TEST(user_handle_table_thread_process_count_includes_other_threads) {
    UH_NEED();
    DWORD g0 = uh_gr(GR_USEROBJECTS);
    struct tctx c; HANDLE th = start_helper(&c);
    if (!th) { t_skip("helper failed"); return; }
    DWORD g1 = uh_gr(GR_USEROBJECTS);
    CHECK(g1 >= g0 + 5); /* window, menu, accel, hook, hdwp (+ implicit helper objects) */
    unsigned sc = uh_scan_owned(-1);
    EXPECT_EQ(g1, sc);
    HELPER_END();
}
TEST(user_handle_table_thread_exit_destroys_thread_owned_keeps_process_owned) {
    UH_NEED();
    struct tctx c; HANDLE th = start_helper(&c);
    if (!th) { t_skip("helper failed"); return; }
    HWND w = c.w; HMENU m = c.m; HACCEL a = c.a; HHOOK hk = c.hook; HDWP dwp = c.dwp;
    stop_helper(&c, th);
    /* win32k thread cleanup destroys windows, hooks and defer-pos structures of the exiting thread */
    int gone = 0;
    for (int i = 0; i < 60 && !gone; i++) {
        uh_entry_t ew, eh, ed;
        gone = !U(IsWindow)(w) && uh_read_h(hk, &eh) && (eh.type != UT_HOOK || eh.uniq != UH_UNIQ(hk) || (eh.flags & 1))
            && uh_read_h(dwp, &ed) && (ed.type != UT_SETWINDOWPOS || ed.uniq != UH_UNIQ(dwp) || (ed.flags & 1));
        if (!gone) Sleep(50);
    }
    CHECK(gone);
    uh_entry_t e;
    CHECK(uh_read_h(w, &e) && (e.type == 0 || e.uniq != UH_UNIQ(w) || (e.flags & 1)));
    /* menus and accelerator tables belong to the process */
    CHECK(U(IsMenu)(m));
    CHECK(uh_read_h(a, &e) && e.type == UT_ACCEL && e.uniq == UH_UNIQ(a) && e.owner == GetCurrentProcessId());
    CHECK(U(DestroyMenu)(m));
    CHECK(U(DestroyAcceleratorTable)(a));
}
TEST(user_handle_table_thread_exit_releases_counts) {
    UH_NEED();
    DWORD g0 = uh_gr(GR_USEROBJECTS);
    struct tctx c; HANDLE th = start_helper(&c);
    if (!th) { t_skip("helper failed"); return; }
    HMENU m = c.m; HACCEL a = c.a;
    stop_helper(&c, th);
    U(DestroyMenu)(m); U(DestroyAcceleratorTable)(a);
    DWORD g1 = 0;
    for (int i = 0; i < 60; i++) { g1 = uh_gr(GR_USEROBJECTS); if (g1 <= g0) break; Sleep(50); }
    CHECK(g1 <= g0);
    DWORD sc = uh_scan_owned(-1);
    EXPECT_EQ(g1, sc);
}
static DWORD WINAPI iswindow_probe(LPVOID p) {
    HWND *a = p; /* a[0] live window of the main thread, a[1] forged-uniq handle */
    U(GetDesktopWindow)();
    int ok = U(IsWindow)(a[0]) && !U(IsWindow)(a[1]);
    DWORD pid; ok = ok && U(GetWindowThreadProcessId)(a[0], &pid) != 0;
    return ok ? 1 : 0;
}
TEST(user_handle_table_thread_validation_is_per_process_not_per_thread) {
    UH_NEED();
    HWND w = uh_make_top(); REQUIRE(w);
    HWND a[2] = { w, (HWND)uh_forge(w, UH_UNIQ(w) == 3 ? 4 : 3) };
    HANDLE t = CreateThread(NULL, 0, iswindow_probe, a, 0, NULL);
    REQUIRE(t);
    WaitForSingleObject(t, 8000);
    DWORD rc = 0; GetExitCodeThread(t, &rc); CloseHandle(t);
    EXPECT_EQ(rc, 1);
    U(DestroyWindow)(w);
}
static DWORD WINAPI count_probe(LPVOID p) {
    unsigned *o = p; U(GetDesktopWindow)();
    o[0] = uh_gr(GR_USEROBJECTS);
    return 0;
}
TEST(user_handle_table_thread_gui_resources_same_in_every_thread) {
    UH_NEED();
    unsigned v = 0;
    HANDLE t = CreateThread(NULL, 0, count_probe, &v, 0, NULL); REQUIRE(t);
    WaitForSingleObject(t, 8000); CloseHandle(t);
    DWORD mine = uh_gr(GR_USEROBJECTS);
    t_note("count in new thread %u, in main thread %lu", v, (unsigned long)mine);
    CHECK((long)v >= (long)mine - 8 && (long)v <= (long)mine + 8); /* thread conversion may add a few implicit objects */
}
static DWORD WINAPI menu_thread(LPVOID p) { U(GetDesktopWindow)(); *(HMENU *)p = U(CreateMenu)(); return 0; }
TEST(user_handle_table_thread_second_thread_reuses_freed_indices) {
    UH_NEED();
    HMENU m = U(CreateMenu)(); REQUIRE(m);
    unsigned idx = UH_IDX(m), uq = UH_UNIQ(m);
    U(DestroyMenu)(m);
    struct { HMENU h; } r = {0};
    HANDLE t = CreateThread(NULL, 0, menu_thread, &r, 0, NULL); REQUIRE(t);
    WaitForSingleObject(t, 8000); CloseHandle(t);
    REQUIRE(r.h);
    if (UH_IDX(r.h) == idx) EXPECT_EQ(UH_UNIQ(r.h), uh_next_uniq(uq)); /* the free list is process-independent */
    else t_info("index %u not reused by the new thread (got %u)", idx, UH_IDX(r.h));
    U(DestroyMenu)(r.h);
}

/* ---- second process ---- */
CHILD(uh_peer) {
    /* argv[1] = "gui"|"plain", argv[2] = parent pid, argv[3] = result file */
    int gui = argc > 1 && !strcmp(argv[1], "gui");
    char nm[96];
    snprintf(nm, sizeof nm, "Local\\win32kt_uh_ready_%s", argv[2]);
    HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, nm);
    snprintf(nm, sizeof nm, "Local\\win32kt_uh_done_%s", argv[2]);
    HANDLE done = OpenEventA(SYNCHRONIZE, FALSE, nm);
    if (!ready || !done) return 2;
    HMENU m[3] = {0};
    char buf[128] = "";
    if (gui) {
        if (!U(GetDesktopWindow)) return 3;
        U(GetDesktopWindow)();
        for (int i = 0; i < 3; i++) m[i] = U(CreateMenu)();
        snprintf(buf, sizeof buf, "%lu %lu", U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS), U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS));
    } else {
        snprintf(buf, sizeof buf, "%d", GetModuleHandleA("user32.dll") ? 1 : 0);
    }
    HANDLE f = CreateFileA(argv[3], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 4;
    DWORD w; WriteFile(f, buf, (DWORD)strlen(buf), &w, NULL); CloseHandle(f);
    SetEvent(ready);
    WaitForSingleObject(done, 12000);
    return 0;
}
struct peer { PROCESS_INFORMATION pi; HANDLE ready, done; char file[MAX_PATH]; HANDLE proc; };
static int peer_start(struct peer *p, const char *mode) {
    char nm[96], extra[MAX_PATH + 64];
    memset(p, 0, sizeof *p);
    snprintf(nm, sizeof nm, "Local\\win32kt_uh_ready_%lu", GetCurrentProcessId());
    p->ready = CreateEventA(NULL, TRUE, FALSE, nm);
    snprintf(nm, sizeof nm, "Local\\win32kt_uh_done_%lu", GetCurrentProcessId());
    p->done = CreateEventA(NULL, TRUE, FALSE, nm);
    snprintf(p->file, sizeof p->file, "%speer_%s.txt", t_tmpdir(), mode);
    snprintf(extra, sizeof extra, "%s %lu \"%s\"", mode, GetCurrentProcessId(), p->file);
    p->proc = t_spawn_child("uh_peer", extra, 0, NULL, NULL, NULL, FALSE, NULL, &p->pi);
    if (!p->proc) return 0;
    if (WaitForSingleObject(p->ready, 15000) != WAIT_OBJECT_0) return 0;
    return 1;
}
static void peer_stop(struct peer *p) {
    SetEvent(p->done);
    if (p->proc) { if (WaitForSingleObject(p->proc, 8000) != WAIT_OBJECT_0) TerminateProcess(p->proc, 1); CloseHandle(p->pi.hThread); CloseHandle(p->proc); }
    CloseHandle(p->ready); CloseHandle(p->done);
}
static void peer_text(struct peer *p, char *out, size_t cap) {
    out[0] = 0; HANDLE f = CreateFileA(p->file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD n = 0; ReadFile(f, out, (DWORD)cap - 1, &n, NULL); out[n] = 0; CloseHandle(f);
}
TEST(user_handle_table_peer_gui_process_count_visible_to_parent) {
    UH_NEED(); REQUIRE(UN(NtUserGetGuiResources));
    struct peer p;
    if (!peer_start(&p, "gui")) { peer_stop(&p); t_skip("peer start failed"); return; }
    char txt[128]; peer_text(&p, txt, sizeof txt);
    unsigned long cu = 0, cg = 0; sscanf(txt, "%lu %lu", &cu, &cg);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pi.dwProcessId);
    REQUIRE(h);
    CHECK(cu >= 3);
    EXPECT_EQ(U(GetGuiResources)(h, GR_USEROBJECTS), cu);
    EXPECT_EQ(UN(NtUserGetGuiResources)(h, GR_USEROBJECTS), cu);
    EXPECT_EQ(U(GetGuiResources)(h, GR_GDIOBJECTS), cg);
    CHECK(U(GetGuiResources)(h, 4) >= cu);
    /* the peer's objects are in the shared table, owned by the peer pid */
    unsigned n; uh_entry_t *t = uh_snap(&n); unsigned menus = 0;
    for (unsigned i = 0; t && i < n; i++) if (t[i].type == UT_MENU && t[i].owner == p.pi.dwProcessId) menus++;
    free(t);
    EXPECT_EQ(menus, 3);
    CloseHandle(h);
    peer_stop(&p);
}
TEST(user_handle_table_peer_process_exit_frees_its_objects) {
    UH_NEED();
    struct peer p;
    if (!peer_start(&p, "gui")) { peer_stop(&p); t_skip("peer start failed"); return; }
    DWORD pid = p.pi.dwProcessId;
    peer_stop(&p); /* peer exits normally; its menus must be reclaimed by process cleanup */
    int left = -1;
    for (int i = 0; i < 80; i++) {
        unsigned n; uh_entry_t *t = uh_snap(&n); left = 0;
        for (unsigned k = 0; t && k < n; k++) if (t[k].type && t[k].owner == pid) left++;
        free(t);
        if (!left) break;
        Sleep(50);
    }
    EXPECT_EQ(left, 0);
}
TEST(user_handle_table_peer_plain_process_has_no_gui_resources) {
    UH_NEED();
    struct peer p;
    if (!peer_start(&p, "plain")) { peer_stop(&p); t_skip("peer start failed"); return; }
    char txt[64]; peer_text(&p, txt, sizeof txt);
    if (strcmp(txt, "0")) { t_skip("peer loaded user32 (%s)", txt); peer_stop(&p); return; }
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pi.dwProcessId); REQUIRE(h);
    SetLastError(UH_SENTINEL);
    /* a process that never made a win32k call has no GUI resources */
    EXPECT_EQ(U(GetGuiResources)(h, GR_USEROBJECTS), 0);
    EXPECT_ERR(UH_SENTINEL); /* Windows 10: 0 and the last error is left alone */
    CloseHandle(h);
    peer_stop(&p);
}

CHILD(uh_pristine) {
    int fails = 0;
    if (t_is_wine()) { printf("SKIP Wine: win32k syscalls unimplemented, user32 preloaded\n"); return 0; }
#define CC(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
    void *ti = (void *)1;
    CC(!GetModuleHandleA("user32.dll"));
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &ti);
    CC(ti == NULL); /* no win32k syscall yet */
    /* On Windows 10 a bare win32u call before user32 is loaded cannot convert the thread (it dies in the callback
     * table setup), so the conversion is the work of the user32 load. */
    CC(LoadLibraryA("user32.dll") != NULL);
    ti = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &ti);
    CC(ti != NULL); /* the user32 load converted the thread to a GUI thread */
    FARPROC f = t_proc_in("win32u.dll", "NtUserGetGuiResources");
    if (!f) { printf("SKIP no win32u\n"); return fails; }
    DWORD r = ((DWORD (WINAPI *)(HANDLE, DWORD))f)((HANDLE)(LONG_PTR)-1, 1);
    printf("NOTE first-syscall NtUserGetGuiResources(self, USEROBJECTS) = %lu err=%lu\n", r, GetLastError());
    /* now go through user32 */
    if (!U(GetDesktopWindow) || !U(CreateMenu)) { printf("SKIP no user32\n"); return fails; }
    HWND d = U(GetDesktopWindow)();
    CC(d != NULL);
    if (!uh_ready()) { printf("SKIP table unavailable\n"); return fails; }
    uh_entry_t e;
    CC(uh_read_h(d, &e) && e.type == UT_WINDOW && e.uniq == UH_UNIQ(d));
    DWORD g0 = uh_gr(GR_USEROBJECTS);
    CC(g0 == uh_scan_owned(-1));
    HMENU m = U(CreateMenu)();
    CC(m != NULL);
    CC(uh_gr(GR_USEROBJECTS) == g0 + 1);
    CC(uh_gr(4) >= g0 + 1);
    CC(uh_read_h(m, &e) && e.type == UT_MENU && e.owner == GetCurrentProcessId());
    U(DestroyMenu)(m);
    CC(uh_gr(GR_USEROBJECTS) == g0);
    return fails;
}
TEST(user_handle_table_pristine_process_connect_state) {
    struct k_child_result r = k_child_run("uh_pristine", NULL, 30000);
    EXPECT_EQ(r.exit_code, 0);
}
