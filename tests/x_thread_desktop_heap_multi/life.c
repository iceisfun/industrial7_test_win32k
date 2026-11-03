/* x_thread_desktop_heap_multi / life: what a thread or a process owned in win32k when it ended.
 *
 * Windows (with their child and owned windows), hooks and carets belong to the creating THREAD and are gone with it: IsWindow
 * turns FALSE, GetWindowThreadProcessId and GetGUIThreadInfo fail, posting to the thread or its windows fails, a handle value
 * is not handed out again for a different window, and the process USER-object count returns to what it was. GDI objects belong to
 * the PROCESS: a memory DC or pen made on a worker still exists, can be used and deleted from the main thread after the worker
 * is gone, and a window class registered on a worker stays registered. Windows of a child process are real windows to the
 * parent while the child lives, report the child's process id, and vanish when the child ends; another process's GDI handle is
 * not usable. The end of a thread is observed with its handle, and the cleanup that follows is awaited by polling a condition
 * that must become true, never by a fixed sleep.
 */
#include "x.h"

static int until_gone(HWND h) { for (int i = 0; i < 1000; i++) { if (!U(IsWindow)(h)) return 1; Sleep(5); } return 0; }
static DWORD gui_user(void) { return U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS); }
static DWORD gui_gdi(void) { return U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS); }
static int user_back_to(DWORD n) { for (int i = 0; i < 1000; i++) { if (gui_user() == n) return 1; Sleep(5); } return 0; }

/* ---- windows ---- */
XT(windows_of_a_finished_worker_are_gone) {
    NEED_ALL;
    struct xt_wk w; HWND h0, h1;
    CHECK(xt_wk_start(&w, 3));
    h0 = w.w[0]; h1 = w.w[2];
    CHECK(U(IsWindow)(h0)); CHECK(U(IsWindow)(h1));
    xt_wk_stop(&w);
    CHECK(until_gone(h0)); CHECK(until_gone(h1));
}
XT(window_of_a_finished_worker_has_no_thread_or_process) {
    NEED_ALL;
    struct xt_wk w; HWND h; DWORD pid = 0xCCCCCCCC;
    CHECK(xt_wk_start(&w, 1));
    h = w.w[0];
    EXPECT_EQ(U(GetWindowThreadProcessId)(h, &pid), w.tid);
    EXPECT_EQ(pid, GetCurrentProcessId());
    xt_wk_stop(&w);
    CHECK(until_gone(h));
    pid = 0xCCCCCCCC;
    SET_ERR(SENT);
    EXPECT_EQ(U(GetWindowThreadProcessId)(h, &pid), 0);
    EXPECT_ERR(E_INVALID_HWND);
    EXPECT_EQ(pid, 0xCCCCCCCCu);                    /* the process id is not written for a window that is gone */
}
XT(guithreadinfo_fails_for_a_finished_worker) {
    NEED_ALL;
    struct xt_wk w; GUITHREADINFO g; DWORD tid;
    CHECK(xt_wk_start(&w, 1));
    tid = w.tid;
    memset(&g, 0, sizeof g); g.cbSize = sizeof g;
    CHECK(U(GetGUIThreadInfo)(tid, &g));
    xt_wk_stop(&w);
    int failed = 0;
    for (int i = 0; i < 1000 && !failed; i++) { memset(&g, 0, sizeof g); g.cbSize = sizeof g; SET_ERR(SENT); failed = !U(GetGUIThreadInfo)(tid, &g); if (!failed) Sleep(5); }
    CHECK(failed);
    EXPECT_ERR(E_INVALID_PARAM);
}
XT(guithreadinfo_succeeds_for_a_live_worker_and_for_the_main_thread) {
    NEED_ALL;
    struct xt_wk w; GUITHREADINFO g;
    CHECK(xt_wk_start(&w, 1));
    memset(&g, 0, sizeof g); g.cbSize = sizeof g;
    CHECK(U(GetGUIThreadInfo)(w.tid, &g));
    memset(&g, 0, sizeof g); g.cbSize = sizeof g;
    CHECK(U(GetGUIThreadInfo)(GetCurrentThreadId(), &g));
    xt_wk_stop(&w);
}
static void job_tree(struct xt_wk *w, void *p) {
    HWND *o = p;
    o[0] = xt_child_window(w->w[0]);
    o[1] = U(CreateWindowExW)(WS_EX_TOOLWINDOW, (LPCWSTR)(ULONG_PTR)U(GetClassLongW)(w->w[0], GCW_ATOM), L"owned", WS_POPUP, -32000, -32000, 4, 4, w->w[0], NULL, GetModuleHandleW(NULL), NULL);
    o[2] = o[0] ? xt_child_window(o[0]) : NULL;
}
XT(child_and_owned_windows_of_a_finished_worker_are_gone_too) {
    NEED_ALL;
    struct xt_wk w; HWND o[3] = { 0 };
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_tree, o));
    CHECK(o[0] && o[1] && o[2]);
    for (int i = 0; i < 3; i++) CHECK(U(IsWindow)(o[i]));
    xt_wk_stop(&w);
    for (int i = 0; i < 3; i++) CHECK(until_gone(o[i]));
}
XT(a_destroyed_worker_window_handle_is_not_handed_out_to_a_new_window) {
    NEED_ALL;
    struct xt_wk w; HWND old, n[8];
    CHECK(xt_wk_start(&w, 1));
    old = w.w[0];
    xt_wk_stop(&w);
    CHECK(until_gone(old));
    int same = 0;
    for (int i = 0; i < 8; i++) { n[i] = xt_window(); if (n[i] == old) same++; }
    EXPECT_EQ(same, 0);
    CHECK(!U(IsWindow)(old));
    for (int i = 0; i < 8; i++) xt_destroy(n[i]);
}
XT(user_object_count_returns_to_baseline_after_a_worker_with_windows_ends) {
    NEED_ALL;
    struct xt_wk w; HWND o[3] = { 0 };
    DWORD base = gui_user();
    CHECK(xt_wk_start(&w, 3));
    CHECK(xt_wk_run(&w, job_tree, o));
    CHECK(gui_user() >= base + 3);
    xt_wk_stop(&w);
    CHECK(user_back_to(base));
}
static void job_hook(struct xt_wk *w, void *p) {
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, (HOOKPROC)U(DefWindowProcW), NULL, GetCurrentThreadId());
    *(HHOOK *)p = h;
}
XT(thread_hook_and_its_user_object_end_with_the_worker) {
    NEED_ALL;
    struct xt_wk w; HHOOK h = NULL;
    DWORD base = gui_user();
    CHECK(xt_wk_start(&w, 1));
    DWORD with_window = gui_user();
    CHECK(xt_wk_run(&w, job_hook, &h));
    if (!h) { t_skip("the hook could not be set"); xt_wk_stop(&w); return; }
    EXPECT_EQ(gui_user(), with_window + 1);
    xt_wk_stop(&w);
    CHECK(user_back_to(base));
    SET_ERR(SENT);
    CHECK(!U(UnhookWindowsHookEx)(h));                     /* the hook went with its thread */
}
static void job_caret_life(struct xt_wk *w, void *p) { U(CreateCaret)(w->w[0], NULL, 2, 6); *(int *)p = 1; }
XT(caret_of_a_finished_worker_leaves_no_user_objects) {
    NEED_ALL;
    struct xt_wk w; int ok = 0;
    DWORD base = gui_user();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_caret_life, &ok));
    xt_wk_stop(&w);
    CHECK(user_back_to(base));
}
XT(several_short_workers_in_a_row_leave_no_user_objects) {
    NEED_ALL;
    DWORD base = gui_user();
    for (int i = 0; i < 4; i++) {
        struct xt_wk w; HWND o[3] = { 0 };
        CHECK(xt_wk_start(&w, 2));
        CHECK(xt_wk_run(&w, job_tree, o));
        xt_wk_stop(&w);
    }
    CHECK(user_back_to(base));
}

/* ---- GDI objects belong to the process ---- */
static void job_gdi(struct xt_wk *w, void *p) {
    HGDIOBJ *o = p;
    DWORD c0 = gui_gdi();
    o[0] = U(CreateCompatibleDC)(NULL);
    DWORD c1 = gui_gdi();
    o[1] = U(CreatePen)(PS_SOLID, 1, RGB(1, 2, 3));
    DWORD c2 = gui_gdi();
    o[2] = U(CreateSolidBrush)(RGB(4, 5, 6));
    DWORD c3 = gui_gdi();
    o[3] = (HGDIOBJ)(ULONG_PTR)(c1 - c0); o[4] = (HGDIOBJ)(ULONG_PTR)(c2 - c1); o[5] = (HGDIOBJ)(ULONG_PTR)(c3 - c2);
}
XT(gdi_objects_made_on_a_worker_outlive_the_worker) {
    NEED_ALL;
    struct xt_wk w; HGDIOBJ o[6] = { 0 };
    DWORD base = gui_gdi();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_gdi, o));
    CHECK(o[0] && o[1] && o[2]);
    DWORD made = gui_gdi() - base;
    EXPECT_EQ((DWORD)(ULONG_PTR)o[3], 1);                 /* a memory DC and a pen count one each */
    EXPECT_EQ((DWORD)(ULONG_PTR)o[4], 1);
    xt_wk_stop(&w);
    EXPECT_EQ(U(GetObjectType)(o[0]), OBJ_MEMDC);
    EXPECT_EQ(U(GetObjectType)(o[1]), OBJ_PEN);
    EXPECT_EQ(U(GetObjectType)(o[2]), OBJ_BRUSH);
    EXPECT_EQ(gui_gdi() - base, made);                   /* none of them left with the worker */
    CHECK(U(DeleteDC)(o[0])); CHECK(U(DeleteObject)(o[1])); CHECK(U(DeleteObject)(o[2]));
    EXPECT_EQ(gui_gdi(), base);
}
XT(a_memory_dc_of_a_finished_worker_is_usable_from_the_main_thread) {
    NEED_ALL;
    struct xt_wk w; HGDIOBJ o[6] = { 0 };
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_gdi, o));
    xt_wk_stop(&w);
    HGDIOBJ old = U(SelectObject)((HDC)o[0], o[1]);
    CHECK(old != NULL);
    EXPECT_EQ(U(GetCurrentObject)((HDC)o[0], OBJ_PEN), o[1]);
    U(SelectObject)((HDC)o[0], old);
    U(DeleteDC)((HDC)o[0]); U(DeleteObject)(o[1]); U(DeleteObject)(o[2]);
}
static void job_class(struct xt_wk *w, void *p) {
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW); wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"win32kt_xt_thrcls";
    *(ATOM *)p = U(RegisterClassExW)(&wc);
}
XT(window_class_registered_on_a_worker_outlives_the_worker) {
    NEED_ALL;
    struct xt_wk w; ATOM a = 0; WNDCLASSEXW wc;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_class, &a));
    CHECK(a != 0);
    xt_wk_stop(&w);
    memset(&wc, 0, sizeof wc); wc.cbSize = sizeof wc;
    CHECK(U(GetClassInfoExW)(GetModuleHandleW(NULL), L"win32kt_xt_thrcls", &wc));
    CHECK(U(UnregisterClassW)(L"win32kt_xt_thrcls", GetModuleHandleW(NULL)));
}
static void job_menu(struct xt_wk *w, void *p) { *(HMENU *)p = U(CreatePopupMenu)(); }
XT(menu_made_on_a_worker_outlives_the_worker) {
    NEED_ALL;
    struct xt_wk w; HMENU m = NULL;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_menu, &m));
    CHECK(m != NULL);
    CHECK(U(IsMenu)(m));
    xt_wk_stop(&w);
    for (int i = 0; i < 200 && U(IsMenu)(m); i++) Sleep(5);
    CHECK(U(IsMenu)(m));                              /* a menu belongs to the process */
    CHECK(U(DestroyMenu)(m));
    CHECK(!U(IsMenu)(m));
}

/* ---- child processes ---- */
XC(mkwin) {
    HWND a = xt_window(), b = xt_window(), c = xt_window();
    CK(a && b && c);
    HDC dc = U(CreateCompatibleDC)(NULL);
    s->v[0] = (LONG64)(ULONG_PTR)a; s->v[1] = (LONG64)(ULONG_PTR)b; s->v[2] = (LONG64)(ULONG_PTR)c;
    s->v[3] = GetCurrentThreadId(); s->v[4] = (LONG64)(ULONG_PTR)dc;
    s->v[5] = U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS);
    s->ready = 1;
    xt_wait(&s->go, 60000);
    return 0;      /* the process ends with its windows and DC */
}
struct kid { HANDLE hm; struct xt_shm *s; HANDLE p; PROCESS_INFORMATION pi; };
static int kid_start(struct kid *k, const char *mode) {
    const char *tag = xt_tag();
    memset(k, 0, sizeof *k);
    k->s = xt_shm_create(tag, &k->hm);
    if (!k->s) return 0;
    k->p = xt_spawn(mode, tag, NULL, &k->pi);
    return k->p && xt_wait(&k->s->ready, 60000);
}
static DWORD kid_end(struct kid *k) {
    DWORD ec = 1;
    if (k->s) k->s->go = 1;
    if (k->p) ec = xt_reap(k->p, &k->pi, 60000);
    if (k->s) UnmapViewOfFile((void *)k->s);
    if (k->hm) CloseHandle(k->hm);
    return ec;
}
XT(child_windows_are_real_windows_to_the_parent_while_the_child_lives) {
    NEED_ALL;
    struct kid k; CHECK(kid_start(&k, "mkwin"));
    HWND a = (HWND)(ULONG_PTR)k.s->v[0], c = (HWND)(ULONG_PTR)k.s->v[2];
    CHECK(U(IsWindow)(a)); CHECK(U(IsWindow)(c));
    EXPECT_EQ(kid_end(&k), 0);
}
XT(child_window_reports_the_child_process_and_thread) {
    NEED_ALL;
    struct kid k; CHECK(kid_start(&k, "mkwin"));
    HWND a = (HWND)(ULONG_PTR)k.s->v[0]; DWORD pid = 0;
    DWORD tid = U(GetWindowThreadProcessId)(a, &pid);
    EXPECT_EQ(pid, k.pi.dwProcessId);
    EXPECT_EQ(tid, (DWORD)k.s->v[3]);
    CHECK(pid != GetCurrentProcessId());
    EXPECT_EQ(kid_end(&k), 0);
}
XT(child_windows_vanish_when_the_child_ends) {
    NEED_ALL;
    struct kid k; CHECK(kid_start(&k, "mkwin"));
    HWND a = (HWND)(ULONG_PTR)k.s->v[0], b = (HWND)(ULONG_PTR)k.s->v[1], c = (HWND)(ULONG_PTR)k.s->v[2];
    EXPECT_EQ(kid_end(&k), 0);
    CHECK(until_gone(a)); CHECK(until_gone(b)); CHECK(until_gone(c));
}
XT(child_windows_do_not_count_as_the_parents_user_objects) {
    NEED_ALL;
    DWORD base = gui_user();
    struct kid k; CHECK(kid_start(&k, "mkwin"));
    EXPECT_EQ(gui_user(), base);
    DWORD kids = U(GetGuiResources)(k.p, GR_USEROBJECTS);
    CHECK(kids >= 3);
    EXPECT_EQ(kid_end(&k), 0);
    EXPECT_EQ(gui_user(), base);
}
XT(gdi_handle_of_a_child_is_not_usable_in_the_parent) {
    NEED_ALL;
    struct kid k; CHECK(kid_start(&k, "mkwin"));
    HDC dc = (HDC)(ULONG_PTR)k.s->v[4];
    EXPECT_EQ(U(GetObjectType)(dc), 0);
    SET_ERR(SENT);
    CHECK(!U(DeleteDC)(dc));
    EXPECT_EQ(kid_end(&k), 0);
}
XT(sendmessage_style_calls_to_a_vanished_child_window_fail_with_invalid_window_handle) {
    NEED_ALL;
    struct kid k; CHECK(kid_start(&k, "mkwin"));
    HWND a = (HWND)(ULONG_PTR)k.s->v[0];
    EXPECT_EQ(kid_end(&k), 0);
    CHECK(until_gone(a));
    SET_ERR(SENT);
    CHECK(!U(PostMessageW)(a, WM_APP + 1, 0, 0));
    EXPECT_ERR(E_INVALID_HWND);
    SET_ERR(SENT);
    EXPECT_EQ(U(SendMessageTimeoutW)(a, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 100, NULL), 0);
    EXPECT_ERR(E_INVALID_HWND);
}
XT(two_children_have_separate_window_sets) {
    NEED_ALL;
    struct kid k1, k2; CHECK(kid_start(&k1, "mkwin")); CHECK(kid_start(&k2, "mkwin"));
    DWORD p1 = 0, p2 = 0;
    U(GetWindowThreadProcessId)((HWND)(ULONG_PTR)k1.s->v[0], &p1);
    U(GetWindowThreadProcessId)((HWND)(ULONG_PTR)k2.s->v[0], &p2);
    EXPECT_EQ(p1, k1.pi.dwProcessId);
    EXPECT_EQ(p2, k2.pi.dwProcessId);
    CHECK(p1 != p2);
    HWND a1 = (HWND)(ULONG_PTR)k1.s->v[0], a2 = (HWND)(ULONG_PTR)k2.s->v[0];
    CHECK(a1 != a2);
    EXPECT_EQ(kid_end(&k1), 0);
    CHECK(until_gone(a1));
    CHECK(U(IsWindow)(a2));                              /* the other child's windows stay */
    EXPECT_EQ(kid_end(&k2), 0);
}
