/* Cross-process input state: a peer process owns a window with focus, active state and a caret.
 * The parent inspects it through GetGUIThreadInfo, tries the per-queue operations on its window and
 * (where UIPI allows it) attaches its input to the peer's thread.  Also a pristine-state child. */
#include "fc.h"

struct peer_info {
    volatile DWORD pid, tid;
    volatile ULONGLONG hwnd;
    volatile LONG ready, focus_ok, active_ok, caret_ok, capture_ok;
    volatile LONG enable_cnt, enable_wp, killfocus_cnt, setfocus_cnt;
};

static LRESULT CALLBACK peer_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static struct peer_info *pi;
    if (m == WM_NCCREATE) pi = NULL;
    if (m == WM_APP) { pi = (struct peer_info *)l; return 0; }
    if (pi) {
        if (m == WM_ENABLE) { pi->enable_wp = (LONG)w; InterlockedIncrement(&pi->enable_cnt); }
        if (m == WM_KILLFOCUS) InterlockedIncrement(&pi->killfocus_cnt);
        if (m == WM_SETFOCUS) InterlockedIncrement(&pi->setfocus_cnt);
    }
    return U(DefWindowProcW)(h, m, w, l);
}

static void names(DWORD ppid, WCHAR *info, WCHAR *stop, WCHAR *ready) {
    _snwprintf(info, 64, L"Local\\win32kt_fc_info_%lu", ppid);
    _snwprintf(stop, 64, L"Local\\win32kt_fc_stop_%lu", ppid);
    _snwprintf(ready, 64, L"Local\\win32kt_fc_ready_%lu", ppid);
}

CHILD(fca_peer) {
    DWORD ppid = argc > 1 ? (DWORD)strtoul(argv[1], NULL, 10) : 0;
    WCHAR ni[64], ns[64], nr[64]; names(ppid, ni, ns, nr);
    HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, ni);
    HANDLE stop = OpenEventW(EVENT_ALL_ACCESS, FALSE, ns), ready = OpenEventW(EVENT_ALL_ACCESS, FALSE, nr);
    if (!map || !stop || !ready) return 2;
    struct peer_info *pi = (struct peer_info *)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof *pi);
    if (!pi || !fc_user()) return 3;
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    WCHAR cls[64]; _snwprintf(cls, 64, L"win32kt_fcp_%lu", GetCurrentProcessId());
    wc.cbSize = sizeof wc; wc.lpfnWndProc = peer_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cls;
    if (!U(RegisterClassExW)(&wc)) return 4;
    HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW, cls, L"fc_peer", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, wc.hInstance, NULL);
    if (!w) return 5;
    U(ShowWindow)(w, SW_SHOWNOACTIVATE);
    U(SendMessageW)(w, WM_APP, 0, (LPARAM)pi);
    U(SetActiveWindow)(w);
    U(SetFocus)(w);
    pi->active_ok = (U(GetActiveWindow)() == w);
    pi->focus_ok = (U(GetFocus)() == w);
    pi->caret_ok = U(CreateCaret)(w, NULL, 3, 4) && U(SetCaretPos)(6, 7) && U(ShowCaret)(w);
    pi->pid = GetCurrentProcessId(); pi->tid = GetCurrentThreadId(); pi->hwnd = (ULONGLONG)(ULONG_PTR)w;
    pi->ready = 1;
    SetEvent(ready);
    DWORD start = GetTickCount();
    MSG m;
    while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0 && GetTickCount() - start < 30000) {
        while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(DispatchMessageW)(&m); }
        Sleep(5);
    }
    return 0;
}

struct peer {
    HANDLE proc, map, stop, ready; PROCESS_INFORMATION pi; struct peer_info *info;
    HWND w; DWORD tid;
};

static int peer_start(struct peer *p) {
    memset(p, 0, sizeof *p);
    if (!fc_user()) return 0;
    WCHAR ni[64], ns[64], nr[64]; names(GetCurrentProcessId(), ni, ns, nr);
    p->map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, ni);
    p->stop = CreateEventW(NULL, TRUE, FALSE, ns);
    p->ready = CreateEventW(NULL, TRUE, FALSE, nr);
    if (!p->map || !p->stop || !p->ready) return 0;
    p->info = (struct peer_info *)MapViewOfFile(p->map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof *p->info);
    if (!p->info) return 0;
    memset((void *)p->info, 0, sizeof *p->info);
    char arg[32]; snprintf(arg, sizeof arg, "%lu", GetCurrentProcessId());
    p->proc = t_spawn_child("fca_peer", arg, 0, NULL, NULL, NULL, FALSE, NULL, &p->pi);
    if (!p->proc) return 0;
    HANDLE hs[2] = { p->ready, p->proc };   /* a peer that dies early must not cost the full timeout */
    if (WaitForMultipleObjects(2, hs, FALSE, 15000) != WAIT_OBJECT_0 || !p->info->ready) return 0;
    p->w = (HWND)(ULONG_PTR)p->info->hwnd; p->tid = p->info->tid;
    return 1;
}
static DWORD peer_stop(struct peer *p) {
    DWORD code = 0xFFFFFFFF;
    if (p->stop) SetEvent(p->stop);
    if (p->proc) {
        if (WaitForSingleObject(p->proc, 8000) != WAIT_OBJECT_0) TerminateProcess(p->proc, 1);
        WaitForSingleObject(p->proc, 3000);
        GetExitCodeProcess(p->proc, &code);
        CloseHandle(p->proc); CloseHandle(p->pi.hThread);
    }
    if (p->info) UnmapViewOfFile((void *)p->info);
    if (p->map) CloseHandle(p->map);
    if (p->stop) CloseHandle(p->stop);
    if (p->ready) CloseHandle(p->ready);
    memset(p, 0, sizeof *p);
    return code;
}
#define PEER_NEED(p) struct peer p; if (!peer_start(&p)) { peer_stop(&p); FC_SKIP("peer process did not start"); return; }

TEST(fca_peer_window_ownership) {
    FC_NEED(); PEER_NEED(p);
    DWORD pid = 0;
    EXPECT_CALL_EQ(U(GetWindowThreadProcessId)(p.w, &pid), p.tid);
    EXPECT_EQ(pid, p.pi.dwProcessId);
    CHECK(U(IsWindow)(p.w));
    CHECK(U(IsWindowEnabled)(p.w));
    CHECK(pid != GetCurrentProcessId());
    EXPECT_CALL_EQ(peer_stop(&p), 0);
}

TEST(fca_peer_gti_sees_peer_queue) {
    FC_NEED(); PEER_NEED(p);
    GUITHREADINFO g;
    SET_ERR(0);
    BOOL r = fc_gti(p.tid, &g);
    CHECKF(r, "GetGUIThreadInfo(peer) failed, err %lu", GetLastError());   /* same desktop: allowed across processes */
    if (r) {
        if (p.info->active_ok) EXPECT_EQ(g.hwndActive, p.w);
        if (p.info->focus_ok) EXPECT_EQ(g.hwndFocus, p.w);
        if (p.info->caret_ok) {
            EXPECT_EQ(g.hwndCaret, p.w);
            EXPECT_EQ(g.flags & GUI_CARETBLINKING, GUI_CARETBLINKING);
            EXPECT_EQ(g.rcCaret.left, 6); EXPECT_EQ(g.rcCaret.top, 7);
            EXPECT_EQ(g.rcCaret.right, 9); EXPECT_EQ(g.rcCaret.bottom, 11);
        }
        EXPECT_EQ(g.hwndCapture, NULL);
    }
    CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_NE(g.hwndFocus, p.w);
    EXPECT_NE(g.hwndCaret, p.w);
    peer_stop(&p);
}

TEST(fca_peer_per_queue_ops_refused) {
    FC_NEED(); PEER_NEED(p);
    HWND mine = fc_top(); REQUIRE(mine);
    fc_quiesce();
    SET_ERR(0);
    CHECK(U(SetFocus)(p.w) == NULL);
    EXPECT_ERR(ERROR_ACCESS_DENIED);             /* SetFocus: window in a different queue */
    CHECK(U(SetActiveWindow)(p.w) == NULL);
    CHECK(U(SetCapture)(p.w) == NULL);
    EXPECT_EQ(U(GetCapture)(), NULL);
    CHECK(!U(CreateCaret)(p.w, NULL, 2, 2));
    GUITHREADINFO g;
    CHECK(fc_gti(p.tid, &g));
    if (p.info->caret_ok) EXPECT_EQ(g.hwndCaret, p.w);   /* peer's caret untouched */
    fc_destroy(mine);
    peer_stop(&p);
}

TEST(fca_peer_enablewindow_cross_process) {
    FC_NEED(); PEER_NEED(p);
    SET_ERR(0);
    BOOL r = U(EnableWindow)(p.w, FALSE);
    DWORD e = GetLastError();
    if (!r && e == ERROR_ACCESS_DENIED) { t_note("EnableWindow across processes refused (UIPI), err 5"); peer_stop(&p); return; }
    EXPECT_EQ(r, FALSE);
    CHECK(!U(IsWindowEnabled)(p.w));              /* client-side bit of the peer's desktop-heap window object */
    for (int i = 0; i < 100 && !p.info->enable_cnt; i++) Sleep(10);
    CHECK(p.info->enable_cnt >= 1);               /* WM_ENABLE delivered to the peer's window procedure */
    EXPECT_EQ(p.info->enable_wp, 0);
    r = U(EnableWindow)(p.w, TRUE);
    CHECK(r);
    CHECK(U(IsWindowEnabled)(p.w));
    peer_stop(&p);
}

TEST(fca_peer_attach_cross_process) {
    FC_NEED(); PEER_NEED(p);
    HWND mine = fc_top(); REQUIRE(mine);
    fc_quiesce();
    DWORD me = GetCurrentThreadId();
    SET_ERR(0);
    BOOL r = U(AttachThreadInput)(me, p.tid, TRUE);
    DWORD e = GetLastError();
    if (!r) {
        t_info("cross-process attach refused: err %lu", e);
        CHECKF(e == ERROR_ACCESS_DENIED || e == ERROR_INVALID_PARAMETER, "unexpected error %lu", e);
    } else {
        HWND f = U(GetFocus)();
        if (p.info->focus_ok) EXPECT_EQ(f, p.w);    /* shared queue: the peer's focus window is ours now */
        HWND a = U(GetActiveWindow)();
        if (p.info->active_ok) EXPECT_EQ(a, p.w);
        U(SetFocus)(p.w);                            /* and the foreign window is no longer foreign */
        EXPECT_EQ(U(GetFocus)(), p.w);
        CHECK(U(AttachThreadInput)(me, p.tid, FALSE));
        SET_ERR(0);
        CHECK(!U(AttachThreadInput)(me, p.tid, FALSE));
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SET_ERR(0);
        CHECK(U(SetFocus)(p.w) == NULL);
        EXPECT_ERR(ERROR_ACCESS_DENIED);
    }
    fc_detach_all(me, p.tid);
    fc_destroy(mine);
    peer_stop(&p);
}

TEST(fca_peer_exit_while_attached) {
    FC_NEED(); PEER_NEED(p);
    HWND mine = fc_top(); REQUIRE(mine);
    fc_quiesce();
    DWORD me = GetCurrentThreadId(), ptid = p.tid;
    if (!U(AttachThreadInput)(me, ptid, TRUE)) { fc_destroy(mine); peer_stop(&p); FC_SKIP("cross-process attach refused"); }
    DWORD code = peer_stop(&p);   /* peer exits while our input is attached to its thread */
    EXPECT_EQ(code, 0);
    SET_ERR(0);
    CHECK(!U(AttachThreadInput)(me, ptid, FALSE));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    HWND f = U(GetFocus)();
    CHECK(f == NULL || U(IsWindow)(f));
    HWND a = U(GetActiveWindow)();
    CHECK(a == NULL || U(IsWindow)(a));
    GUITHREADINFO g;
    CHECK(fc_gti(me, &g));
    fc_destroy(mine);
}

/* ---- pristine process: first win32k calls happen in the child ------------------------------ */

#define CC(c) do { if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
CHILD(fca_fresh_queue) {
    int fails = 0;
    if (!fc_user()) return 99;
    /* The very first win32k call (GetGUIThreadInfo) both converts the thread and reports its empty queue. */
    GUITHREADINFO g;
    memset(&g, 0xCC, sizeof g); g.cbSize = sizeof g;
    BOOL r = U(GetGUIThreadInfo)(GetCurrentThreadId(), &g);
    if (!r) { printf("NOTE first GetGUIThreadInfo failed err=%lu\n", GetLastError()); }
    else {
        CC(g.flags == 0);
        CC(g.hwndActive == NULL); CC(g.hwndFocus == NULL); CC(g.hwndCapture == NULL);
        CC(g.hwndMenuOwner == NULL); CC(g.hwndMoveSize == NULL); CC(g.hwndCaret == NULL);
        CC(g.rcCaret.left == 0 && g.rcCaret.right == 0);
    }
    void *twi = NULL;
    CC(k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &twi));
    CC(twi != NULL);                          /* a win32k call has converted the thread */
    CC(U(GetFocus)() == NULL);
    CC(U(GetActiveWindow)() == NULL);
    CC(U(GetCapture)() == NULL);
    CC(U(ReleaseCapture)());                  /* succeeds with nothing captured */
    SetLastError(0);
    CC(!U(SetCaretPos)(1, 1));
    CC(GetLastError() == ERROR_ACCESS_DENIED);
    SetLastError(0);
    CC(!U(DestroyCaret)());
    CC(GetLastError() == ERROR_ACCESS_DENIED);
    SetLastError(0);
    CC(!U(AttachThreadInput)(GetCurrentThreadId(), GetCurrentThreadId(), TRUE));
    CC(GetLastError() == ERROR_INVALID_PARAMETER);
    SetLastError(0);
    CC(U(SetFocus)(NULL) == NULL);
    CC(U(SetActiveWindow)(NULL) == NULL);
    CC(!U(GetInputState)());                  /* nothing queued in a fresh queue */
    return fails;
}

TEST(fca_peer_fresh_process_queue_state) {
    struct k_child_result r = k_child_run("fca_fresh_queue", "", 30000);
    EXPECT_EQ(r.exit_code, 0);
}
