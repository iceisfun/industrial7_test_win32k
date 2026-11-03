/* process_session_cleanup / threads: what the kernel does when a GUI thread dies.
 *
 * thread teardown (windows, hooks, timers, hotkeys, queues of
 * the thread are freed giving the handles back and decrementing the per-process object count).
 * GDI objects are owned by the PROCESS (the pid is recorded as owner, NtGdiDeleteObjectApp accepts
 * pid == process or 0), so a dead thread's brushes/bitmaps/regions stay valid until the process exits.
 * Everything here runs in the test process: threads are short lived and joined.
 */
#include "psc.h"

#define NEED_GDI() do { REQUIRE(U(GetGuiResources)); REQUIRE(U(CreateSolidBrush)); } while (0)
#define NEED_UI() do { REQUIRE(U(GetGuiResources)); REQUIRE(U(CreateWindowExA)); REQUIRE(U(IsWindow)); REQUIRE(U(CreateSolidBrush)); \
    HWND probe_ = psc_mk(NULL, "probe"); if (!probe_) { t_skip("cannot create a window (no display driver)"); return; } U(DestroyWindow)(probe_); } while (0)

static DWORD user_count(void) { return psc_gr(PSC_SELF, PSC_USER); }
static DWORD gdi_count(void) { return psc_gr(PSC_SELF, PSC_GDI); }

static LRESULT CALLBACK hook_proc(int code, WPARAM w, LPARAM l) { return U(CallNextHookEx)(NULL, code, w, l); }

/* ---- thread bodies --------------------------------------------------------------------------- */
enum { TK_WND1, TK_WND25, TK_TREE, TK_HOOK, TK_TIMER_T, TK_TIMER_W, TK_HOOK_WND_TIMER, TK_MSGONLY, TK_NOTHING, TK_COUNT };
static const char *const tk_name[TK_COUNT] = { "window", "windows25", "tree", "hook", "thread_timer", "window_timer", "all_kinds", "message_only", "nothing" };
static const int tk_alive_windows[TK_COUNT] = { 1, 25, 7, 0, 0, 1, 1, 1, 0 };

static void tk_fn(struct psc_th *t) {
    int kind = (int)(ULONG_PTR)t->user;
    switch (kind) {
    case TK_WND1: t->hw[0] = psc_mk(NULL, "tk1"); break;
    case TK_WND25: for (int i = 0; i < 25; i++) { HWND w = psc_mk(NULL, "tk25"); if (i < 8) t->hw[i] = w; } break;
    case TK_TREE: {                       /* 1 top + 3 children + 3 owned popups = 7 windows */
        HWND top = psc_mk(NULL, "top");
        t->hw[0] = top;
        for (int i = 0; i < 3; i++) { psc_mk(top, "child"); psc_mk_owned(top, "owned"); }
        t->hw[1] = U(GetWindow)(top, GW_CHILD);
        break;
    }
    case TK_HOOK: t->r[0] = (ULONGLONG)(ULONG_PTR)U(SetWindowsHookExA)(WH_GETMESSAGE, hook_proc, NULL, GetCurrentThreadId()); break;
    case TK_TIMER_T: t->r[0] = U(SetTimer)(NULL, 0, 60000, NULL); break;
    case TK_TIMER_W: t->hw[0] = psc_mk(NULL, "tw"); t->r[0] = U(SetTimer)(t->hw[0], 7, 60000, NULL); break;
    case TK_HOOK_WND_TIMER:
        t->hw[0] = psc_mk(NULL, "all");
        t->r[0] = (ULONGLONG)(ULONG_PTR)U(SetWindowsHookExA)(WH_GETMESSAGE, hook_proc, NULL, GetCurrentThreadId());
        t->r[1] = U(SetTimer)(t->hw[0], 9, 60000, NULL);
        t->r[2] = U(SetTimer)(NULL, 0, 60000, NULL);
        break;
    case TK_MSGONLY: t->hw[0] = U(CreateWindowExA)(0, psc_class(), "mo", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL); break;
    case TK_NOTHING: psc_warm(); break;
    }
    t->ok = 1;
    psc_park(t);
}

/* ---- 1. window ownership while the thread lives, destruction when it dies ------------------- */
TEST(process_session_cleanup_thread_window_alive_state) {
    NEED_UI();
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND1));
    HWND h = t.hw[0];
    CHECK(h != NULL);
    CHECK(U(IsWindow)(h));
    /* user32!IsWindow's fast path: type byte 1, deleted bit clear, uniqueness word == HIWORD(handle) */
    CHECK(psc_user_live(h));
    ULONG_PTR tid = 0;
    CHECK(psc_user_owner_tid(h, &tid));
    CHECKF(tid == t.tid, "entry+8 holds %llu, owner thread is %lu", (unsigned long long)tid, (unsigned long)t.tid);
    DWORD pid = 0;
    DWORD got = U(GetWindowThreadProcessId)(h, &pid);
    CHECK(got == t.tid);
    CHECK(pid == GetCurrentProcessId());
    CHECK(psc_th_end(&t));
}
TEST(process_session_cleanup_thread_window_destroyed_after_exit) {
    NEED_UI();
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND1));
    HWND h = t.hw[0];
    CHECK(psc_th_end(&t));
    CHECK(PSC_POLL(!U(IsWindow)(h), 3000));
    CHECK(!psc_user_live(h));
    CHECK(k_wnd_ptr(h) == NULL);
}
TEST(process_session_cleanup_thread_window_handle_entry_freed) {
    NEED_UI();
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND1));
    HWND h = t.hw[0];
    struct k_user_entry before, after;
    REQUIRE(k_user_entry(h, &before));
    CHECK(before.type == K_TYPE_WINDOW);
    CHECK(psc_th_end(&t));
    PSC_POLL(!U(IsWindow)(h), 3000);
    REQUIRE(k_user_entry(h, &after));
    /* the freed slot returns to the free list: it no longer reads as a live window, or its uniqueness moved on */
    CHECKF(after.type != K_TYPE_WINDOW || (after.flags & 1) || after.uniq != (unsigned short)((ULONG_PTR)h >> 16),
           "entry type %u flags 0x%02x uniq 0x%04x", after.type, after.flags, after.uniq);
}
TEST(process_session_cleanup_thread_exit_does_not_touch_other_threads_windows) {
    NEED_UI();
    psc_warm_process();
    HWND mine = psc_mk(NULL, "mine");
    REQUIRE(mine);
    DWORD u0 = user_count();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND25));
    CHECK(psc_th_end(&t));
    CHECK(PSC_POLL(user_count() == u0, 3000));
    CHECK(U(IsWindow)(mine));
    CHECK(psc_user_live(mine));
    CHECK(U(DestroyWindow)(mine));
}
TEST(process_session_cleanup_thread_second_thread_windows_survive_first) {
    NEED_UI();
    psc_warm_process();
    struct psc_th a, b;
    REQUIRE(psc_th_start(&a, tk_fn, (void *)(ULONG_PTR)TK_WND1));
    REQUIRE(psc_th_start(&b, tk_fn, (void *)(ULONG_PTR)TK_WND1));
    HWND ha = a.hw[0], hb = b.hw[0];
    CHECK(ha != hb);
    CHECK(psc_th_end(&a));
    CHECK(PSC_POLL(!U(IsWindow)(ha), 3000));
    CHECK(U(IsWindow)(hb));
    CHECK(U(GetWindowThreadProcessId)(hb, NULL) == b.tid);
    CHECK(psc_th_end(&b));
    CHECK(PSC_POLL(!U(IsWindow)(hb), 3000));
}

/* ---- 2. USER object accounting returns to the baseline for every kind of thread-owned object ----- */
static void user_baseline(int kind) {
    NEED_UI();
    psc_warm_process();
    DWORD u0 = user_count();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)kind));
    DWORD alive = user_count();
    if (tk_alive_windows[kind]) {
        /* every window is one thread-owned object counted per process */
        CHECKF(alive >= u0 + (DWORD)tk_alive_windows[kind], "%s: baseline %lu alive %lu", tk_name[kind], u0, alive);
    }
    t_note("%s: USER count baseline %lu, alive %lu", tk_name[kind], u0, alive);
    CHECK(psc_th_end(&t));
    DWORD after = 0;
    int ok = PSC_POLL((after = user_count()) == u0, 3000);
    CHECKF(ok, "%s: baseline %lu, after thread exit %lu", tk_name[kind], u0, after);
    CHECK(psc_gr(PSC_SELF, PSC_USER_PEAK) >= alive);
}
#define UB_T(k) TEST(process_session_cleanup_thread_user_count_baseline_##k) { user_baseline(TK_##k); }
UB_T(WND1) UB_T(WND25) UB_T(TREE) UB_T(HOOK) UB_T(TIMER_T) UB_T(TIMER_W) UB_T(HOOK_WND_TIMER) UB_T(MSGONLY) UB_T(NOTHING)

TEST(process_session_cleanup_thread_tree_all_windows_destroyed) {
    NEED_UI();
    psc_warm_process();
    DWORD u0 = user_count();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_TREE));
    HWND top = t.hw[0], child = t.hw[1];
    CHECK(top && child);
    CHECK(U(IsWindow)(child));
    CHECK(U(GetParent)(child) == top);
    CHECK(psc_th_end(&t));
    CHECK(PSC_POLL(!U(IsWindow)(top), 3000));
    CHECK(!U(IsWindow)(child));          /* children go with the parent */
    CHECK(PSC_POLL(user_count() == u0, 3000));
}
TEST(process_session_cleanup_thread_25_windows_all_gone) {
    NEED_UI();
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND25));
    HWND hs[8];
    memcpy(hs, t.hw, sizeof hs);
    for (int i = 0; i < 8; i++) CHECK(U(IsWindow)(hs[i]));
    CHECK(psc_th_end(&t));
    for (int i = 0; i < 8; i++) CHECKF(PSC_POLL(!U(IsWindow)(hs[i]), 3000), "window %d still valid", i);
}
TEST(process_session_cleanup_thread_message_only_window_destroyed) {
    NEED_UI();
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_MSGONLY));
    HWND h = t.hw[0];
    if (!h) { t_skip("message-only window creation failed (%lu)", GetLastError()); psc_th_end(&t); return; }
    CHECK(U(IsWindow)(h));
    CHECK(U(GetParent)(h) == HWND_MESSAGE || U(GetAncestor)(h, GA_PARENT) != NULL);
    CHECK(psc_th_end(&t));
    CHECK(PSC_POLL(!U(IsWindow)(h), 3000));
}

/* ---- 3. hooks ------------------------------------------------------------------------------- */
TEST(process_session_cleanup_thread_hook_handle_dies_with_thread) {
    NEED_UI();
    REQUIRE(U(UnhookWindowsHookEx));
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_HOOK));
    HHOOK hk = (HHOOK)(ULONG_PTR)t.r[0];
    if (!hk) { t_skip("SetWindowsHookEx failed"); psc_th_end(&t); return; }
    struct k_user_entry e;
    if (k_user_entry(hk, &e)) CHECKF(e.type == 5, "hook entry type %u", e.type);   /* HMValidateHandle(h, 5) in NtUserUnhookWindowsHookEx */
    CHECK(psc_th_end(&t));
    CHECK(PSC_POLL(k_user_entry(hk, &e) && (e.type != 5 || (e.flags & 1)), 3000) || !k_user_entry(hk, &e));
    SetLastError(PSC_MAGIC);
    BOOL r = U(UnhookWindowsHookEx)(hk);
    DWORD err = GetLastError();
    CHECK(!r);                           /* HMValidateHandle fails: zzzUnhookWindowsHookEx is never reached, result 0 */
    t_note("UnhookWindowsHookEx(dead hook) last error %lu", err);
}
TEST(process_session_cleanup_thread_hook_removed_by_owner_before_exit) {
    NEED_UI();
    psc_warm_process();
    DWORD u0 = user_count();
    HHOOK hk = U(SetWindowsHookExA)(WH_GETMESSAGE, hook_proc, NULL, GetCurrentThreadId());
    REQUIRE(hk);
    CHECK(user_count() >= u0);
    CHECK(U(UnhookWindowsHookEx)(hk));
    CHECK(user_count() == u0);
    SetLastError(PSC_MAGIC);
    CHECK(!U(UnhookWindowsHookEx)(hk));   /* second unhook: handle no longer a hook object */
}

/* ---- 4. timers --------------------------------------------------------------------------------- */
TEST(process_session_cleanup_thread_window_timer_killed_with_window) {
    NEED_UI();
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_TIMER_W));
    HWND h = t.hw[0];
    CHECK(t.r[0] == 7);                  /* SetTimer(hwnd, id, ...) returns the id for a window timer */
    CHECK(psc_th_end(&t));
    PSC_POLL(!U(IsWindow)(h), 3000);
    SetLastError(PSC_MAGIC);
    CHECK(!U(KillTimer)(h, 7));          /* both the window and its timer are gone */
}
TEST(process_session_cleanup_thread_thread_timer_killable_from_other_thread_of_process) {
    NEED_UI();
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_TIMER_T));
    UINT_PTR id = (UINT_PTR)t.r[0];
    CHECK(id != 0);
    /* Windows 10 finds a thread timer (hwnd NULL) by id among the timers of the process: the main thread, which owns
     * no such timer itself, removes the other thread's timer. */
    SetLastError(PSC_MAGIC);
    CHECK(U(KillTimer)(NULL, id));
    CHECK(psc_th_end(&t));
    SetLastError(PSC_MAGIC);
    CHECK(!U(KillTimer)(NULL, id));      /* and it is gone: a second kill finds nothing */
}

/* ---- 5. leaked GDI objects survive their creating thread ---------------------------------------- */
struct leak_ctx { int kind; int n; HGDIOBJ h[64]; };
static void leak_fn(struct psc_th *t) {
    struct leak_ctx *c = (struct leak_ctx *)t->user;
    for (int i = 0; i < c->n; i++) c->h[i] = psc_mk_kind(c->kind);
    t->ok = 1;
    psc_park(t);
}
static void leak_case(int kind, int n) {
    NEED_GDI();
    psc_warm_process();
    struct psc_drain dr; psc_drain_take(&dr);        /* no cache hits: every leaked object below is a fresh handle */
    DWORD g0 = gdi_count();
    static struct leak_ctx c;
    memset(&c, 0, sizeof c);
    c.kind = kind; c.n = n;
    struct psc_th t;
    REQUIRE(psc_th_start(&t, leak_fn, &c));
    DWORD alive = gdi_count();
    CHECK(alive >= g0 + (DWORD)n);
    CHECK(psc_th_end(&t));
    Sleep(50);
    DWORD after = gdi_count();
    /* the owner recorded in each cell is the process id (gdi owner = pid), not the thread: nothing is freed at thread exit */
    CHECKF(after == alive, "%s: GDI count alive %lu, after thread exit %lu", psc_kind_name[kind], alive, after);
    int live = 0, valid = 0;
    for (int i = 0; i < n; i++) {
        unsigned owner = 0;
        if (c.h[i] && psc_cell_live(c.h[i], &owner) && owner == GetCurrentProcessId()) live++;
        if (c.h[i] && U(GetObjectType)(c.h[i]) != 0) valid++;
    }
    CHECKF(live == n, "%s: %d of %d cells still owned by this process", psc_kind_name[kind], live, n);
    CHECKF(valid == n, "%s: %d of %d handles still valid for GetObjectType", psc_kind_name[kind], valid, n);
    int deleted = 0;
    for (int i = 0; i < n; i++) if (c.h[i] && psc_del_kind(kind, c.h[i])) deleted++;
    CHECKF(deleted == n, "%s: main thread could delete %d of %d leaked objects", psc_kind_name[kind], deleted, n);
    { DWORD g1 = gdi_count(); CHECKF(g1 >= g0 && g1 - g0 <= PSC_CACHE_MAX, "after deleting the leaked objects %lu, before %lu", g1, g0); }
    psc_drain_give(&dr);
}
#define LEAK_T(nm, k, n) TEST(process_session_cleanup_thread_leaked_##nm##_##n##_survives_exit) { leak_case(k, n); }
LEAK_T(brush, PK_BRUSH, 1) LEAK_T(brush, PK_BRUSH, 20) LEAK_T(pen, PK_PEN, 5) LEAK_T(rgn, PK_RGN, 5)
LEAK_T(bitmap, PK_BITMAP, 5) LEAK_T(palette, PK_PALETTE, 3) LEAK_T(dc, PK_DC, 3) LEAK_T(font, PK_FONT, 3) LEAK_T(dib, PK_DIB, 3)

TEST(process_session_cleanup_thread_leaked_object_cell_owner_is_pid) {
    NEED_UI();
    psc_warm_process();
    static struct leak_ctx c;
    memset(&c, 0, sizeof c);
    c.kind = PK_BRUSH; c.n = 1;
    struct psc_th t;
    REQUIRE(psc_th_start(&t, leak_fn, &c));
    HGDIOBJ h = c.h[0];
    struct psc_cell before;
    REQUIRE(psc_cell_of(h, &before));
    CHECKF((before.owner & ~1u) == GetCurrentProcessId(), "owner 0x%x, pid %lu", before.owner, (unsigned long)GetCurrentProcessId());
    CHECK((before.owner & ~1u) != t.tid);
    CHECK(psc_th_end(&t));
    struct psc_cell after;
    REQUIRE(psc_cell_of(h, &after));
    CHECK(after.owner == before.owner && after.upper == before.upper && after.objt == before.objt);
    U(DeleteObject)(h);
}
TEST(process_session_cleanup_thread_main_selects_leaked_brush) {
    NEED_UI();
    psc_warm_process();
    static struct leak_ctx c;
    memset(&c, 0, sizeof c);
    c.kind = PK_BRUSH; c.n = 1;
    struct psc_th t;
    REQUIRE(psc_th_start(&t, leak_fn, &c));
    CHECK(psc_th_end(&t));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HGDIOBJ old = U(SelectObject)(dc, c.h[0]);
    CHECK(old != NULL);
    CHECK(U(GetCurrentObject)(dc, OBJ_BRUSH) == c.h[0]);
    U(SelectObject)(dc, old);
    CHECK(U(DeleteObject)(c.h[0]));
    CHECK(U(DeleteDC)(dc));
}

/* ---- 6. dead windows through the user32 entry points ------------------------------------------- */
static HWND dead_window(void) {
    struct psc_th t;
    if (!psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND1)) return NULL;
    HWND h = t.hw[0];
    psc_th_end(&t);
    PSC_POLL(!U(IsWindow)(h), 3000);
    return h;
}
#define DEAD_T(nm, ...) TEST(process_session_cleanup_dead_window_##nm) { NEED_UI(); psc_warm_process(); HWND h = dead_window(); REQUIRE(h); \
    CHECK(!U(IsWindow)(h)); SetLastError(PSC_MAGIC); __VA_ARGS__ }

DEAD_T(sendmessage, { CHECK(U(SendMessageA)(h, WM_NULL, 0, 0) == 0); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(sendmessage_wm_user, { CHECK(U(SendMessageA)(h, WM_USER + 1, 1, 2) == 0); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(sendmessage_timeout, { DWORD_PTR res = 0x55; LRESULT r = U(SendMessageTimeoutA)(h, WM_NULL, 0, 0, SMTO_NORMAL, 500, &res); CHECK(r == 0); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(sendmessage_wide, { CHECK(U(SendMessageW)(h, WM_NULL, 0, 0) == 0); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(postmessage, { CHECK(!U(PostMessageA)(h, WM_NULL, 0, 0)); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(postmessage_quit, { CHECK(!U(PostMessageA)(h, WM_QUIT, 0, 0)); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(sendnotify, { CHECK(!U(SendNotifyMessageA)(h, WM_NULL, 0, 0)); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(sendmessage_callback_not_called, { CHECK(!U(SendMessageCallbackA)(h, WM_NULL, 0, 0, NULL, 0)); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(getwindowthreadprocessid, {
    DWORD pid = 0x1234;
    CHECK(U(GetWindowThreadProcessId)(h, &pid) == 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);   /* user32!GetWindowThreadProcessId: RtlSetLastWin32Error(0x578) when the table entry is not a live window */
    CHECKF(pid == 0x1234, "pid out-parameter must not be written: %lu", pid);
})
DEAD_T(getwindowthreadprocessid_null_pid, { CHECK(U(GetWindowThreadProcessId)(h, NULL) == 0); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(getwindowrect, { RECT r = { 1, 2, 3, 4 }; CHECK(!U(GetWindowRect)(h, &r)); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(getclientrect, { RECT r = { 1, 2, 3, 4 }; CHECK(!U(GetClientRect)(h, &r)); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(destroywindow, { CHECK(!U(DestroyWindow)(h)); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(setwindowpos, { CHECK(!U(SetWindowPos)(h, NULL, 0, 0, 1, 1, SWP_NOZORDER | SWP_NOACTIVATE)); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(getwindowlong, { CHECK(U(GetWindowLongA)(h, GWL_STYLE) == 0); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(getwindowlongptr_exstyle, { CHECK(U(GetWindowLongPtrA)(h, GWL_EXSTYLE) == 0); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); })
DEAD_T(getclassname, { char b[64] = "x"; CHECK(U(GetClassNameA)(h, b, sizeof b) == 0); t_note("GetClassName(dead) error %lu", GetLastError()); })
DEAD_T(getwindowtext, { char b[64] = "x"; CHECK(U(GetWindowTextA)(h, b, sizeof b) == 0); t_note("GetWindowText(dead) error %lu", GetLastError()); })
DEAD_T(getparent, { CHECK(U(GetParent)(h) == NULL); t_note("GetParent(dead) error %lu", GetLastError()); })
DEAD_T(getdc, { HDC dc = U(GetDC)(h); CHECK(dc == NULL); if (dc) U(ReleaseDC)(h, dc); t_note("GetDC(dead) error %lu", GetLastError()); })
DEAD_T(getwindowdc, { HDC dc = U(GetWindowDC)(h); CHECK(dc == NULL); if (dc) U(ReleaseDC)(h, dc); t_note("GetWindowDC(dead) error %lu", GetLastError()); })
DEAD_T(visible_enabled_flags, {
    CHECK(!U(IsWindowVisible)(h));
    CHECK(!U(IsWindowEnabled)(h));
    CHECK(!U(IsIconic)(h));
    CHECK(!U(IsZoomed)(h));
    CHECK(!U(IsChild)(h, h));
})
DEAD_T(showwindow, { CHECK(!U(ShowWindow)(h, SW_HIDE)); t_note("ShowWindow(dead) error %lu", GetLastError()); })
DEAD_T(invalidaterect, { CHECK(!U(InvalidateRect)(h, NULL, FALSE)); t_note("InvalidateRect(dead) error %lu", GetLastError()); })
DEAD_T(setforeground, { CHECK(!U(SetForegroundWindow)(h)); t_note("SetForegroundWindow(dead) error %lu", GetLastError()); })
DEAD_T(setfocus, { CHECK(U(SetFocus)(h) == NULL || GetFocus() != h); CHECK(GetFocus() != h); })
DEAD_T(setcapture_not_taken, { U(SetCapture)(h); CHECK(U(GetCapture)() != h); U(ReleaseCapture)(); })
DEAD_T(setparent, { HWND old = U(SetParent)(h, NULL); CHECK(old == NULL); t_note("SetParent(dead) error %lu", GetLastError()); })
DEAD_T(enablewindow, { CHECK(!U(EnableWindow)(h, TRUE)); t_note("EnableWindow(dead) error %lu", GetLastError()); })
DEAD_T(queryquery_window_via_stub, {
    NtUserQueryWindow_pfn q = UN(NtUserQueryWindow);
    REQUIRE(q);
    if (t_is_wine()) { t_skip("direct syscall stub unsupported by the emulator"); return; }
    /* NtUserQueryWindow: ValidateHwnd fails -> falls to the "return 0" arm for every information class */
    for (ULONG c = 0; c <= 9; c++) CHECKF(q(h, c) == 0, "class %lu", c);
})
DEAD_T(ishungappwindow, { CHECK(!U(IsHungAppWindow)(h)); })   /* user32!IsHungAppWindow = NtUserQueryWindow(h, 5) != 0 */
DEAD_T(findwindow_does_not_list_it, {
    BOOL found = FALSE;
    HWND w = NULL;
    while ((w = U(FindWindowExA)(NULL, w, psc_class(), NULL)) != NULL) if (w == h) found = TRUE;
    CHECK(!found);
})

/* ---- 7. messages racing the thread exit ----------------------------------------------------------- */
TEST(process_session_cleanup_thread_crossthread_send_reply_identifies_owner) {
    NEED_UI();
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND1));
    LRESULT r = U(SendMessageA)(t.hw[0], PSC_M_TID, 0, 0);
    CHECKF((DWORD)r == t.tid, "reply %llu, owner thread %lu", (unsigned long long)r, (unsigned long)t.tid);   /* the window procedure runs on the owning thread */
    CHECK(psc_th_end(&t));
}
TEST(process_session_cleanup_thread_posted_messages_processed_by_owner) {
    NEED_UI();
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND1));
    LONG c0 = psc_msg_count;
    for (int i = 0; i < 5; i++) CHECK(U(PostMessageA)(t.hw[0], PSC_M_COUNT, 0, 0));
    CHECK(PSC_POLL(psc_msg_count == c0 + 5, 3000));
    CHECK(psc_th_end(&t));
}
TEST(process_session_cleanup_thread_send_to_exiting_thread_returns) {
    NEED_UI();
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND1));
    HWND h = t.hw[0];
    DWORD t0 = GetTickCount();
    DWORD_PTR res = 0x77;
    SetLastError(PSC_MAGIC);
    /* the receiver calls ExitThread inside its window procedure: the kernel must release the blocked sender */
    LRESULT r = U(SendMessageTimeoutA)(h, PSC_M_EXITTHREAD, 0, 0, SMTO_ABORTIFHUNG, 3000, &res);
    DWORD dt = GetTickCount() - t0;
    /* the sender is released and the call counts as completed: TRUE with a zero result and no error set */
    CHECKF(r == 1, "SendMessageTimeout returned %lld", (long long)r);
    CHECKF(res == 0, "result slot %llu", (unsigned long long)res);
    EXPECT_ERR(PSC_MAGIC);
    CHECKF(dt < 5000, "sender released after %lu ms", dt);
    t_note("SendMessageTimeout to a thread that exits inside the handler: result %lld last error %lu res %llu", (long long)r, GetLastError(), (unsigned long long)res);
    CHECK(WaitForSingleObject(t.h, 5000) == WAIT_OBJECT_0);
    CHECK(PSC_POLL(!U(IsWindow)(h), 3000));
    psc_th_end(&t);
}
TEST(process_session_cleanup_thread_post_thread_message_to_dead_thread) {
    NEED_UI();
    REQUIRE(U(PostThreadMessageA));
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND1));
    DWORD tid = t.tid;
    CHECK(U(PostThreadMessageA)(tid, WM_NULL, 0, 0));      /* alive: the queue exists */
    psc_th_end(&t);
    SetLastError(PSC_MAGIC);
    CHECK(!U(PostThreadMessageA)(tid, WM_NULL, 0, 0));
    EXPECT_ERR(ERROR_INVALID_THREAD_ID);
}
TEST(process_session_cleanup_thread_post_thread_message_unknown_tid) {
    NEED_UI();
    SetLastError(PSC_MAGIC);
    CHECK(!U(PostThreadMessageA)(0xFFFFFFF0u, WM_NULL, 0, 0));
    EXPECT_ERR(ERROR_INVALID_THREAD_ID);
}
TEST(process_session_cleanup_thread_get_gui_thread_info_dead_tid) {
    NEED_UI();
    REQUIRE(U(GetGUIThreadInfo));
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND1));
    DWORD tid = t.tid;
    GUITHREADINFO gi; memset(&gi, 0, sizeof gi); gi.cbSize = sizeof gi;
    CHECK(U(GetGUIThreadInfo)(tid, &gi));      /* alive and GUI: succeeds */
    psc_th_end(&t);
    memset(&gi, 0, sizeof gi); gi.cbSize = sizeof gi;
    SetLastError(PSC_MAGIC);
    BOOL ok = U(GetGUIThreadInfo)(tid, &gi);
    CHECK(!ok);
    t_note("GetGUIThreadInfo(dead tid) error %lu", GetLastError());
}

/* ---- 8. hotkeys ------------------------------------------------------------------------------------ */
#define HK_MODS (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN)
#define HK_VK VK_F24
#define HK_ID 0x3131
static void hk_fn(struct psc_th *t) {
    int mode = (int)(ULONG_PTR)t->user;         /* 0 window hotkey, 1 thread hotkey (hwnd NULL), 2 window hotkey, window destroyed while thread lives */
    HWND w = psc_mk(NULL, "hk");
    t->hw[0] = w;
    t->r[0] = U(RegisterHotKey)(mode == 1 ? NULL : w, HK_ID, HK_MODS, HK_VK);
    t->r[1] = GetLastError();
    if (mode == 2 && t->r[0]) { U(DestroyWindow)(w); }
    t->ok = 1;
    psc_park(t);
}
static void hotkey_case(int mode) {
    NEED_UI();
    REQUIRE(U(RegisterHotKey));
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, hk_fn, (void *)(ULONG_PTR)mode));
    if (!t.r[0]) { t_skip("hotkey Ctrl+Alt+Shift+Win+F24 not available (error %llu)", (unsigned long long)t.r[1]); psc_th_end(&t); return; }
    SetLastError(PSC_MAGIC);
    BOOL dup = U(RegisterHotKey)(NULL, HK_ID + 1, HK_MODS, HK_VK);
    DWORD err = GetLastError();
    if (mode == 2) {
        /* the owning window was destroyed while the thread keeps running: the hotkey must already be free (HKRemoveMatchingHotkeys on window destruction) */
        CHECK(dup);
        if (dup) U(UnregisterHotKey)(NULL, HK_ID + 1);
        CHECK(psc_th_end(&t));
        return;
    }
    CHECK(!dup);
    CHECKF(err == ERROR_HOTKEY_ALREADY_REGISTERED, "error %lu", err);
    if (dup) U(UnregisterHotKey)(NULL, HK_ID + 1);
    CHECK(psc_th_end(&t));
    /* DestroyThreadsHotKeys: HKRemoveMatchingHotkeys(ptiCurrent, NULL, 0, 0) at thread teardown */
    BOOL again = FALSE;
    int ok = PSC_POLL((again = U(RegisterHotKey)(NULL, HK_ID + 2, HK_MODS, HK_VK)) != 0, 3000);
    CHECKF(ok, "hotkey still held after the owner thread died (error %lu)", GetLastError());
    if (again) CHECK(U(UnregisterHotKey)(NULL, HK_ID + 2));
}
TEST(process_session_cleanup_thread_hotkey_window_released_on_exit) { hotkey_case(0); }
TEST(process_session_cleanup_thread_hotkey_thread_released_on_exit) { hotkey_case(1); }
TEST(process_session_cleanup_thread_hotkey_released_when_window_destroyed) { hotkey_case(2); }
TEST(process_session_cleanup_thread_hotkey_other_threads_window_rejected) {
    NEED_UI();
    REQUIRE(U(RegisterHotKey));
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND1));
    SetLastError(PSC_MAGIC);
    /* _RegisterHotKey: hWnd >= 2 must belong to the calling thread, else ERROR_WINDOW_OF_OTHER_THREAD */
    BOOL ok = U(RegisterHotKey)(t.hw[0], HK_ID + 3, HK_MODS, HK_VK);
    DWORD err = GetLastError();
    CHECK(!ok);
    CHECKF(err == ERROR_WINDOW_OF_OTHER_THREAD, "error %lu", err);
    if (ok) U(UnregisterHotKey)(t.hw[0], HK_ID + 3);
    CHECK(psc_th_end(&t));
}
TEST(process_session_cleanup_thread_hotkey_dead_window_rejected) {
    NEED_UI();
    REQUIRE(U(RegisterHotKey));
    psc_warm_process();
    HWND h = dead_window();
    REQUIRE(h);
    SetLastError(PSC_MAGIC);
    BOOL ok = U(RegisterHotKey)(h, HK_ID + 4, HK_MODS, HK_VK);
    CHECK(!ok);
    t_note("RegisterHotKey(dead window) error %lu", GetLastError());
    if (ok) U(UnregisterHotKey)(h, HK_ID + 4);
}

/* ---- 9. churn: many short-lived GUI threads leave no residue --------------------------------------- */
TEST(process_session_cleanup_thread_churn_sequential_threads_user_count) {
    NEED_UI();
    psc_warm_process();
    DWORD u0 = user_count(), g0 = gdi_count();
    for (int i = 0; i < 24; i++) {
        struct psc_th t;
        if (!psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)(i % 2 ? TK_WND25 : TK_HOOK_WND_TIMER))) { CHECKF(0, "thread %d did not start", i); break; }
        psc_th_end(&t);
    }
    DWORD u1 = 0;
    CHECKF(PSC_POLL((u1 = user_count()) == u0, 4000), "USER count %lu before, %lu after 24 threads", u0, u1);
    CHECK(gdi_count() == g0);
}
TEST(process_session_cleanup_thread_churn_parallel_threads) {
    NEED_UI();
    psc_warm_process();
    DWORD u0 = user_count();
    struct psc_th t[12];
    int started = 0;
    for (int i = 0; i < 12; i++) if (psc_th_start(&t[i], tk_fn, (void *)(ULONG_PTR)TK_TREE)) started++; else { psc_th_end(&t[i]); break; }
    CHECK(started == 12);
    CHECK(user_count() >= u0 + 12 * 7);
    for (int i = 0; i < started; i++) CHECK(psc_th_end(&t[i]));
    DWORD u1 = 0;
    CHECKF(PSC_POLL((u1 = user_count()) == u0, 4000), "USER count %lu before, %lu after", u0, u1);
}
TEST(process_session_cleanup_thread_peak_not_lowered_by_thread_exit) {
    NEED_UI();
    psc_warm_process();
    struct psc_th t;
    REQUIRE(psc_th_start(&t, tk_fn, (void *)(ULONG_PTR)TK_WND25));
    DWORD alive = user_count();
    DWORD peak_alive = psc_gr(PSC_SELF, PSC_USER_PEAK);
    CHECK(peak_alive >= alive);
    psc_th_end(&t);
    PSC_POLL(user_count() < alive, 3000);
    CHECK(psc_gr(PSC_SELF, PSC_USER_PEAK) >= peak_alive);     /* peak = max(peak, count), never decremented */
    CHECK(user_count() < alive);
}
static void cls_fn(struct psc_th *t) {
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = psc_wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = (const char *)t->user;
    t->r[0] = U(RegisterClassA)(&wc);
    t->ok = 1;
    psc_park(t);
}
TEST(process_session_cleanup_thread_class_outlives_registering_thread) {
    NEED_UI();
    REQUIRE(U(UnregisterClassA));
    static char name[64];
    snprintf(name, sizeof name, "win32kt_psc_cls_%lu", (unsigned long)GetCurrentProcessId());
    struct psc_th t;
    /* window classes belong to the process, so the thread that registered one can die without taking it along */
    REQUIRE(psc_th_start(&t, cls_fn, name));
    REQUIRE(t.r[0]);
    CHECK(psc_th_end(&t));
    WNDCLASSA out;
    CHECK(U(GetClassInfoA)(GetModuleHandleA(NULL), name, &out));
    CHECK(U(UnregisterClassA)(name, GetModuleHandleA(NULL)));
    SetLastError(PSC_MAGIC);
    CHECK(!U(GetClassInfoA)(GetModuleHandleA(NULL), name, &out));
    EXPECT_ERR(ERROR_CLASS_DOES_NOT_EXIST);
}
