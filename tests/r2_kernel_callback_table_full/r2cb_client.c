/* r2_kernel_callback_table_full: the client callbacks the kernel makes on its own initiative, and the
 * hook / event / APC / menu / icon paths that reach them from legitimate user code.
 *
 * Kernel callers (win32kfull analysis; KeUserModeCallback number == table slot):
 *   84 client ThreadSetup      (0, 0)                      first win32k call of a thread
 *   85 ClientDeliverUserApc      (0, 0)                      MsgWait/GetMessage wait returned STATUS_USER_APC (0xC0)
 *   76 client LoadMenu         len == *(u32*)buf           class menu of a new top-level window
 *   64 client CopyImage        handle, type, cx, cy, flags class small icon created with the first window
 *   88 client CallWinEventProc 0x30 bytes                  out-of-context WinEvent delivered to the hooking thread
 *   42 fnHkINLPCBTCREATESTRUCT   0x2a, HCBT_CREATEWND        40 fnHkINDWORD (0x28) HCBT_DESTROYWND
 *   47 fnHkINLPMSG               0x2f, len 0x58              WH_GETMESSAGE
 */
#include "r2cb.h"

#define NEED_HOOK() do { R2_NEED(r2_gui_connect(), "no GUI connection / KernelCallbackTable"); \
                         R2_NEED(r2_install(), "cannot install the private callback table"); } while (0)

#define SetWindowsHookExA   U(SetWindowsHookExA)
#define GetClassLongPtrW    U(GetClassLongPtrW)
#define LoadIconW           U(LoadIconW)
#define CopyIcon            U(CopyIcon)
#define DestroyIcon         U(DestroyIcon)
#define GetSystemMetrics    U(GetSystemMetrics)

static const unsigned char *find_bytes(const unsigned char *h, size_t hl, const void *n, size_t nl) {
    if (!nl || hl < nl) return NULL;
    for (size_t i = 0; i + nl <= hl; i++) if (!memcmp(h + i, n, nl)) return h + i;
    return NULL;
}

/* ---- ClientThreadSetup (84) ----------------------------------------------------------------------- */
static DWORD WINAPI gui_thread(LPVOID p) {
    /* first win32k call converts the thread: ntoskrnl/win32k run client ThreadSetup through the table */
    IsWindow((HWND)0);
    *(DWORD *)p = GetCurrentThreadId();
    for (int i = 0; i < 4; i++) { IsWindow((HWND)0); HWND h = k_make_window("r2cb-ts"); if (h) DestroyWindow(h); }
    return 0;
}
static DWORD WINAPI plain_thread(LPVOID p) { *(DWORD *)p = GetCurrentThreadId(); Sleep(30); return 0; }

R2_SCN(threadsetup_fires_once_per_new_gui_thread_with_empty_buffer) {
    NEED_HOOK();
    DWORD tids[3] = { 0, 0, 0 };
    LONG m = r2_mark();
    for (int i = 0; i < 3; i++) {
        HANDLE t = CreateThread(NULL, 0, gui_thread, &tids[i], 0, NULL);
        R2_NEED(t, "no thread");
        WaitForSingleObject(t, 8000);
        CloseHandle(t);
    }
    for (int i = 0; i < 3; i++) {
        int n = 0; struct r2_ev *first = NULL;
        for (int j = 0; ; j++) { struct r2_ev *e = r2_find(84, m, j); if (!e) break; if (e->tid == tids[i]) { n++; if (!first) first = e; } }
        KF(tids[i] != 0, "thread %d did not run", i);
        KW(n == 1);                                      /* once per thread, not per win32k call */
        if (first) {
            KEQ(first->len, 0);                          /* KeUserModeCallback(0x54, NULL, 0, ...): empty; the dispatcher still hands the thunk a stack address */
            KW((ULONG)first->r8 == 84);
        }
    }
    return 0;
}

R2_SCN(threadsetup_is_the_first_callback_on_a_new_thread) {
    NEED_HOOK();
    DWORD tid = 0;
    LONG m = r2_mark();
    HANDLE t = CreateThread(NULL, 0, gui_thread, &tid, 0, NULL);
    R2_NEED(t, "no thread");
    WaitForSingleObject(t, 8000);
    CloseHandle(t);
    struct r2_ev *first = NULL;
    for (LONG i = m; i < (r2_ev_n < R2_NEV ? r2_ev_n : R2_NEV); i++) if (r2_ev_log[i].tid == tid) { first = &r2_ev_log[i]; break; }
    R2_NEED(first, "no callback on the new thread");
    KW(first->idx == 84);                                /* thread conversion happens before any window callback */
    return 0;
}

R2_SCN(threadsetup_not_requested_for_threads_without_win32k_calls) {
    NEED_HOOK();
    DWORD tid = 0;
    LONG m = r2_mark();
    HANDLE t = CreateThread(NULL, 0, plain_thread, &tid, 0, NULL);
    R2_NEED(t, "no thread");
    WaitForSingleObject(t, 5000);
    CloseHandle(t);
    for (LONG i = m; i < (r2_ev_n < R2_NEV ? r2_ev_n : R2_NEV); i++) KF(r2_ev_log[i].tid != tid, "thread %lu without win32k calls received slot %u", (unsigned long)tid, r2_ev_log[i].idx);
    return 0;
}

/* ---- ClientDeliverUserApc (85) ---------------------------------------------------------------------- */
static volatile LONG g_apc_runs; static volatile DWORD g_apc_tid;
static VOID CALLBACK apc_fn(ULONG_PTR p) { g_apc_tid = GetCurrentThreadId(); InterlockedIncrement(&g_apc_runs); }

R2_SCN(user_apc_to_gui_thread_wait_goes_through_slot_85) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "apc", 0), "no worker");
    Sleep(60);                                           /* let it settle into its alertable MsgWaitForMultipleObjectsEx */
    g_apc_runs = 0;
    LONG m = r2_mark();
    K(QueueUserAPC(apc_fn, w.thread, 0));
    for (int i = 0; i < 60 && g_apc_runs == 0; i++) Sleep(50);
    KEQ(g_apc_runs, 1);
    KEQ(g_apc_tid, w.tid);                               /* APC ran on the target thread */
    /* Windows 10 22H2: an alertable MsgWaitForMultipleObjectsEx that is interrupted by a user APC runs the APC without
     * the kernel calling ClientDeliverUserApc, so slot 85 stays silent for this wait. */
    struct r2_ev *e = r2_find(85, m, 0);
    KW(e == NULL);
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(user_apc_slot_85_hit_per_wait_not_per_apc) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "apc3", 0), "no worker");
    Sleep(60);
    g_apc_runs = 0;
    LONG m = r2_mark();
    for (int i = 0; i < 3; i++) { K(QueueUserAPC(apc_fn, w.thread, 0)); Sleep(80); }
    for (int i = 0; i < 60 && g_apc_runs < 3; i++) Sleep(50);
    KEQ(g_apc_runs, 3);
    int hits = r2_since(85, m);
    KNOTE("3 APCs queued 80 ms apart: %d ClientDeliverUserApc callbacks", hits);
    KW(hits == 0);                                       /* see user_apc_to_gui_thread_wait_goes_through_slot_85: this wait delivers APCs without slot 85 */
    r2_wk_stop(&w);
    return 0;
}

/* ---- ClientLoadMenu (76) ---------------------------------------------------------------------------- */
static LRESULT CALLBACK menu_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcW(h, m, wp, lp); }
static BOOL reg_menu_class(WCHAR *cls, size_t cap, const char *tag, LPCWSTR menu) {
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    _snwprintf(cls, cap - 1, L"win32ktR2_mn_%hs_%lu", tag, GetCurrentProcessId());
    wc.lpfnWndProc = menu_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cls; wc.lpszMenuName = menu;
    return RegisterClassW(&wc) != 0;
}
static HWND make_menu_window(LPCWSTR cls) {
    return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"r2menu", WS_OVERLAPPEDWINDOW, -32000, -32000, 100, 80, NULL, NULL, GetModuleHandleW(NULL), NULL);
}

R2_SCN(loadmenu_named_class_menu_string_in_buffer) {
    NEED_HOOK();
    WCHAR cls[64], name[64];
    _snwprintf(name, 63, L"R2NoSuchMenu_%lu", GetCurrentProcessId());
    R2_NEED(reg_menu_class(cls, 64, "str", name), "no class");
    LONG m = r2_mark();
    HWND h = make_menu_window(cls);
    R2_NEED(h, "no window");
    struct r2_ev *e = r2_find(76, m, 0);
    R2_NEED(e, "no slot-76 callback for a class with a menu name");
    KEQ(e->tid, GetCurrentThreadId());
    KEQ(r2_d(e, 0), e->len);                             /* KeUserModeCallback(0x4c, buf, *(u32*)buf) */
    KEQ(r2_q(e, 0x28), (ULONG_PTR)GetModuleHandleW(NULL));       /* the class's hInstance */
    KEQ(r2_w(e, 0x30), wcslen(name) * 2);                /* UNICODE_STRING.Length of the menu name */
    KEQ(r2_w(e, 0x32), wcslen(name) * 2 + 2);            /* .MaximumLength */
    K(find_bytes(e->data, e->ncopy, name, wcslen(name) * 2) != NULL);
    KEQ(r2_d(e, 8), 1);                                  /* CAPTUREBUF.cCapturedPointers: one captured string */
    /* the resource does not exist: LoadMenuW fails, the thunk returns NULL, the window still gets created */
    K(GetMenu(h) == NULL);
    DestroyWindow(h);
    UnregisterClassW(cls, GetModuleHandleW(NULL));
    return 0;
}

R2_SCN(loadmenu_integer_resource_id_travels_in_the_pointer_field) {
    NEED_HOOK();
    WCHAR cls[64];
    R2_NEED(reg_menu_class(cls, 64, "id", MAKEINTRESOURCEW(7777)), "no class");
    LONG m = r2_mark();
    HWND h = make_menu_window(cls);
    R2_NEED(h, "no window");
    struct r2_ev *e = r2_find(76, m, 0);
    R2_NEED(e, "no slot-76 callback");
    /* RtlInitUnicodeStringOrId: an integer id has Length == MaximumLength == 0 and the id as the "buffer";
     * client LoadMenu then stores it verbatim at buf+0x38 without capturing anything. */
    KEQ(r2_w(e, 0x30), 0);
    KEQ(r2_w(e, 0x32), 0);
    KEQ(r2_q(e, 0x38), 7777);
    KEQ(e->len, 0x40);
    KEQ(r2_q(e, 0x28), (ULONG_PTR)GetModuleHandleW(NULL));
    K(GetMenu(h) == NULL);
    DestroyWindow(h);
    UnregisterClassW(cls, GetModuleHandleW(NULL));
    return 0;
}

static HMENU g_ovr_menu;
static void menu_override(void *buf, ULONG len, unsigned idx) { r2_cbreturn((ULONG_PTR)g_ovr_menu, 0, NULL, 0x18, 0); }
R2_SCN(loadmenu_result_handle_becomes_the_window_menu) {
    NEED_HOOK();
    WCHAR cls[64];
    R2_NEED(reg_menu_class(cls, 64, "ovr", MAKEINTRESOURCEW(7778)), "no class");
    g_ovr_menu = CreateMenu();
    R2_NEED(g_ovr_menu, "no menu");
    r2_ovr[76] = menu_override;
    LONG m = r2_mark();
    HWND h = make_menu_window(cls);
    R2_NEED(h, "no window");
    struct r2_ev *e = r2_find(76, m, 0);
    R2_NEED(e, "no slot-76 callback");
    /* client LoadMenu: HMValidateHandleNoRip(*(qword*)out, TYPE_MENU) and the window takes that menu */
    KW(GetMenu(h) == g_ovr_menu);
    KW(IsMenu(g_ovr_menu));
    r2_ovr[76] = NULL;
    DestroyWindow(h);
    KNOTE("menu still valid after DestroyWindow: %d (window destroyed it: expected 0)", (int)IsMenu(g_ovr_menu));
    if (IsMenu(g_ovr_menu)) DestroyMenu(g_ovr_menu);
    UnregisterClassW(cls, GetModuleHandleW(NULL));
    return 0;
}

R2_SCN(loadmenu_not_requested_without_a_class_menu) {
    NEED_HOOK();
    WCHAR cls[64];
    R2_NEED(reg_menu_class(cls, 64, "none", NULL), "no class");
    LONG m = r2_mark();
    HWND h = make_menu_window(cls);
    R2_NEED(h, "no window");
    KEQ(r2_since(76, m), 0);
    DestroyWindow(h);
    UnregisterClassW(cls, GetModuleHandleW(NULL));
    return 0;
}

/* ---- ClientCopyImage (64): class small icon built with the first window --------------------------- */
static BOOL reg_icon_class(WCHAR *cls, size_t cap, const char *tag, HICON ico) {
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    _snwprintf(cls, cap - 1, L"win32ktR2_ic_%hs_%lu", tag, GetCurrentProcessId());
    wc.lpfnWndProc = menu_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cls; wc.hIcon = ico;
    return RegisterClassW(&wc) != 0;
}
R2_SCN(copyimage_builds_the_class_small_icon_on_first_window) {
    NEED_HOOK();
    HICON ico = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    R2_NEED(ico, "no system icon");
    WCHAR cls[64];
    R2_NEED(reg_icon_class(cls, 64, "a", ico), "no class");
    LONG m = r2_mark();
    HWND h = make_menu_window(cls);
    R2_NEED(h, "no window");
    struct r2_ev *e = r2_find(64, m, 0);
    /* window creation -> class small icon creation (class has hIcon, no small icon yet) -> client CopyImage */
    R2_NEED(e, "no slot-64 callback");
    KEQ(e->tid, GetCurrentThreadId());
    KEQ(r2_q(e, 0), (ULONG_PTR)ico);                    /* source handle */
    KEQ(r2_d(e, 8), IMAGE_ICON);
    KEQ(r2_d(e, 0x14), 0x4000);                         /* LR_COPYFROMRESOURCE */
    K((int)r2_d(e, 0xc) > 0);
    K((int)r2_d(e, 0x10) > 0);
    KNOTE("requested small icon size %dx%d, SM_CXSMICON=%d SM_CYSMICON=%d", (int)r2_d(e, 0xc), (int)r2_d(e, 0x10), GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    HICON sm = (HICON)GetClassLongPtrW(h, GCLP_HICONSM);
    KW(sm != NULL);
    KW(sm != ico);                                      /* a copy, not the source */
    /* a second window of the same class reuses the small icon: no further copy */
    LONG m2 = r2_mark();
    HWND h2 = make_menu_window(cls);
    if (h2) { KEQ(r2_since(64, m2), 0); DestroyWindow(h2); }
    DestroyWindow(h);
    UnregisterClassW(cls, GetModuleHandleW(NULL));
    return 0;
}

R2_SCN(copyimage_not_requested_for_class_without_icon) {
    NEED_HOOK();
    WCHAR cls[64];
    R2_NEED(reg_icon_class(cls, 64, "none", NULL), "no class");
    LONG m = r2_mark();
    HWND h = make_menu_window(cls);
    R2_NEED(h, "no window");
    KEQ(r2_since(64, m), 0);
    DestroyWindow(h);
    UnregisterClassW(cls, GetModuleHandleW(NULL));
    return 0;
}

static HICON g_ovr_icon;
static void icon_override(void *buf, ULONG len, unsigned idx) { r2_cbreturn((ULONG_PTR)g_ovr_icon, 0, NULL, 0x18, 0); }
R2_SCN(copyimage_result_handle_is_taken_as_the_small_icon) {
    NEED_HOOK();
    HICON ico = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    /* The class takes the handle the thunk returned as its small icon and destroys it with the class, so a shared system icon
     * would be invalidated for every later process of the session: hand it a private copy. */
    HICON stock = LoadIconW(NULL, MAKEINTRESOURCEW(32514));
    g_ovr_icon = stock ? CopyIcon(stock) : NULL;
    R2_NEED(ico && g_ovr_icon && ico != g_ovr_icon, "no system icons");
    WCHAR cls[64];
    R2_NEED(reg_icon_class(cls, 64, "ovr", ico), "no class");
    r2_ovr[64] = icon_override;
    LONG m = r2_mark();
    HWND h = make_menu_window(cls);
    R2_NEED(h, "no window");
    R2_NEED(r2_find(64, m, 0), "no slot-64 callback");
    r2_ovr[64] = NULL;
    HICON sm = (HICON)GetClassLongPtrW(h, GCLP_HICONSM);
    KNOTE("class small icon %p, handed back by the hook %p", sm, g_ovr_icon);
    /* Windows 10: the handle the thunk returned becomes the class small icon, at high and at medium integrity alike. */
    int taken = (sm == g_ovr_icon);
    KW(taken);
    DestroyWindow(h);
    UnregisterClassW(cls, GetModuleHandleW(NULL));
    if (!taken) DestroyIcon(g_ovr_icon);                /* a class that took the handle destroys it with the class */
    return 0;
}

/* ---- ClientCallWinEventProc (88) --------------------------------------------------------------------- */
static struct { volatile LONG calls; HWINEVENTHOOK hook; DWORD event; HWND hwnd; LONG idObject, idChild; DWORD tid, time, cur_tid; } g_we;
static VOID CALLBACK we_proc(HWINEVENTHOOK h, DWORD ev, HWND hwnd, LONG obj, LONG child, DWORD tid, DWORD time) {
    g_we.calls++; g_we.hook = h; g_we.event = ev; g_we.hwnd = hwnd; g_we.idObject = obj; g_we.idChild = child; g_we.tid = tid; g_we.time = time;
    g_we.cur_tid = GetCurrentThreadId();
}
R2_SCN(winevent_out_of_context_callback_carries_the_event_arguments) {
    NEED_HOOK();
    HWND h = k_make_window("r2cb-we");
    R2_NEED(h, "no window");
    memset(&g_we, 0, sizeof g_we);
    /* 0x101 is in the OEM-defined range: nothing else in the system generates it */
    HWINEVENTHOOK hook = SetWinEventHook(0x101, 0x101, NULL, we_proc, GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
    R2_NEED(hook, "SetWinEventHook failed");
    LONG m = r2_mark();
    NotifyWinEvent(0x101, h, OBJID_CLIENT, 7);
    for (int i = 0; i < 40 && g_we.calls == 0; i++) k_pump(50);
    struct r2_ev *e = r2_find(88, m, 0);
    KEQ(g_we.calls, 1);
    KW(e != NULL);
    if (e) {
        KEQ(e->tid, GetCurrentThreadId());
        KEQ(e->len, 0x30);                              /* KeUserModeCallback(0x58, &buf, 0x30, ...) */
        KEQ(r2_q(e, 0), (ULONG_PTR)we_proc);            /* the thunk calls [buf+0] */
        KEQ(r2_q(e, 8), (ULONG_PTR)hook);
        KEQ(r2_d(e, 0x10), 0x101);
        KEQ(r2_q(e, 0x18), (ULONG_PTR)h);
        KEQ((LONG)r2_d(e, 0x20), OBJID_CLIENT);
        KEQ(r2_d(e, 0x24), 7);
        KEQ(r2_d(e, 0x28), GetCurrentThreadId());
        K(r2_d(e, 0x2c) != 0);                          /* dwmsEventTime */
    }
    KEQ(g_we.hook, (ULONG_PTR)hook);
    KEQ(g_we.event, 0x101);
    KEQ(g_we.hwnd, (ULONG_PTR)h);
    KEQ(g_we.idObject, OBJID_CLIENT);
    KEQ(g_we.idChild, 7);
    KEQ(g_we.tid, GetCurrentThreadId());
    KEQ(g_we.cur_tid, GetCurrentThreadId());
    UnhookWinEvent(hook);
    DestroyWindow(h);
    return 0;
}

R2_SCN(winevent_each_notification_is_one_callback) {
    NEED_HOOK();
    HWND h = k_make_window("r2cb-we2");
    R2_NEED(h, "no window");
    memset(&g_we, 0, sizeof g_we);
    HWINEVENTHOOK hook = SetWinEventHook(0x102, 0x103, NULL, we_proc, GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
    R2_NEED(hook, "SetWinEventHook failed");
    LONG m = r2_mark();
    NotifyWinEvent(0x102, h, OBJID_CLIENT, 1);
    NotifyWinEvent(0x103, h, OBJID_CLIENT, 2);
    NotifyWinEvent(0x104, h, OBJID_CLIENT, 3);          /* outside the hook's range: filtered in the kernel */
    for (int i = 0; i < 40 && g_we.calls < 2; i++) k_pump(50);
    k_pump(100);
    KEQ(g_we.calls, 2);
    KW(r2_since(88, m) == 2);
    UnhookWinEvent(hook);
    DestroyWindow(h);
    return 0;
}

/* ---- WH_CBT (42, 40) and WH_GETMESSAGE (47): fnHk* callouts ----------------------------------------- */
static struct { volatile LONG create_calls, destroy_calls, other; int ncode; WPARAM wp; LPARAM lp; HWND created; CREATESTRUCTW cs; CBT_CREATEWNDW cbt; HWND insert_after; HWND destroyed; } g_cbt;
static LRESULT CALLBACK cbt_proc(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_CREATEWND) {
        g_cbt.create_calls++; g_cbt.wp = wp; g_cbt.lp = lp; g_cbt.created = (HWND)wp;
        CBT_CREATEWNDW *c = (CBT_CREATEWNDW *)lp;
        if (!IsBadReadPtr(c, sizeof *c)) { g_cbt.cbt = *c; if (c->lpcs && !IsBadReadPtr(c->lpcs, sizeof(CREATESTRUCTW))) g_cbt.cs = *c->lpcs; g_cbt.insert_after = c->hwndInsertAfter; }
        return 0;
    }
    if (code == HCBT_DESTROYWND) { g_cbt.destroy_calls++; g_cbt.destroyed = (HWND)wp; return 0; }
    g_cbt.other++;
    return CallNextHookEx(NULL, code, wp, lp);
}
static LRESULT CALLBACK cs_plain_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcW(h, m, wp, lp); }

R2_SCN(cbt_createwnd_hook_uses_slot_42_with_createstruct_in_buffer) {
    NEED_HOOK();
    WCHAR cls[64]; WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    _snwprintf(cls, 63, L"win32ktR2_cbt_%lu", GetCurrentProcessId());
    wc.lpfnWndProc = cs_plain_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cls;
    R2_NEED(RegisterClassW(&wc), "no class");
    memset(&g_cbt, 0, sizeof g_cbt);
    HHOOK hk = SetWindowsHookExW(WH_CBT, cbt_proc, NULL, GetCurrentThreadId());
    R2_NEED(hk, "SetWindowsHookEx(WH_CBT) failed");
    LONG m = r2_mark();
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"r2cbt", WS_POPUP, 100, 200, 300, 400, NULL, NULL, wc.hInstance, (LPVOID)0x5150);
    R2_NEED(h, "no window");
    KEQ(g_cbt.create_calls, 1);
    KEQ(g_cbt.created, h);
    struct r2_ev *e = r2_find(42, m, 0);
    KW(e != NULL);
    r2_dump("slot 42 HCBT_CREATEWND", e, 0xc0);
    if (e) {
        KEQ(e->tid, GetCurrentThreadId());
        K(e->len >= 0xa8);
        KEQ(r2_d(e, 0x30) & 0xffff, HCBT_CREATEWND);   /* nCode in the low word; the high word (5 here) is not interpreted */
        KEQ(r2_q(e, 0x38), (ULONG_PTR)h);               /* wParam: the new window */
        KEQ(r2_q(e, 0x98), (ULONG_PTR)cbt_proc);        /* the hook procedure the thunk dispatches to */
        KEQ(r2_d(e, 0xa0), 0);                          /* Unicode hook: DispatchHookW */
        /* CREATESTRUCT embedded at buf+0x40 */
        KEQ(r2_q(e, 0x40), 0x5150);
        KEQ(r2_q(e, 0x48), (ULONG_PTR)wc.hInstance);
        KEQ((int)r2_d(e, 0x60), 400); KEQ((int)r2_d(e, 0x64), 300); KEQ((int)r2_d(e, 0x68), 200); KEQ((int)r2_d(e, 0x6c), 100);
        K((r2_d(e, 0x70) & WS_POPUP) != 0);
        /* lParam seen by the hook is the CBT_CREATEWND on the thunk's stack, its lpcs points at buf+0x40 */
        KEQ((ULONG_PTR)g_cbt.cbt.lpcs, (ULONG_PTR)e->buf + 0x40);
        KEQ(g_cbt.cs.x, 100); KEQ(g_cbt.cs.y, 200); KEQ(g_cbt.cs.cx, 300); KEQ(g_cbt.cs.cy, 400);
    }
    /* destroying it goes through fnHkINDWORD (slot 40) with HCBT_DESTROYWND */
    LONG m2 = r2_mark();
    DestroyWindow(h);
    KEQ(g_cbt.destroy_calls, 1);
    KEQ(g_cbt.destroyed, h);
    int found = 0;
    for (int i = 0; ; i++) {
        struct r2_ev *d = r2_find(40, m2, i);
        if (!d) break;
        r2_dump("slot 40 callback", d, 0x40);
        /* nCode is in the low word of one dword and the window handle in a qword of the first 0x40 bytes (the layout differs from
         * the plain 0x30-byte family: see the dump) */
        for (unsigned o = 0; o + 8 <= 0x40 && o + 8 <= d->ncopy; o += 4)
            if ((r2_d(d, o) & 0xffff) == HCBT_DESTROYWND && o + 8 <= 0x38 && r2_q(d, o + 8) == (ULONG_PTR)h) { found = 1; KNOTE("HCBT_DESTROYWND at +0x%x, window at +0x%x, len 0x%lx", o, o + 8, (unsigned long)d->len); }
    }
    KW(found);
    UnhookWindowsHookEx(hk);
    UnregisterClassW(cls, wc.hInstance);
    return 0;
}

R2_SCN(cbt_ansi_hook_sets_the_ansi_flag_in_the_buffer) {
    NEED_HOOK();
    WCHAR cls[64]; WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    _snwprintf(cls, 63, L"win32ktR2_cbta_%lu", GetCurrentProcessId());
    wc.lpfnWndProc = cs_plain_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cls;
    R2_NEED(RegisterClassW(&wc), "no class");
    memset(&g_cbt, 0, sizeof g_cbt);
    HHOOK hk = SetWindowsHookExA(WH_CBT, (HOOKPROC)cbt_proc, NULL, GetCurrentThreadId());
    R2_NEED(hk, "SetWindowsHookExA failed");
    LONG m = r2_mark();
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"r2cbta", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wc.hInstance, NULL);
    R2_NEED(h, "no window");
    struct r2_ev *e = r2_find(42, m, 0);
    KW(e != NULL);
    if (e) {
        KEQ(r2_d(e, 0x30) & 0xffff, HCBT_CREATEWND);
        KEQ(r2_d(e, 0xa0), 1);                          /* the ANSI flag: the thunk picks DispatchHookA */
        KEQ(r2_q(e, 0x98), (ULONG_PTR)cbt_proc);
    }
    DestroyWindow(h);
    UnhookWindowsHookEx(hk);
    UnregisterClassW(cls, wc.hInstance);
    return 0;
}

static void cbt_cancel_override(void *buf, ULONG len, unsigned idx) {
    /* answer HCBT_CREATEWND with a nonzero hook result; the 0x58 bytes of CREATESTRUCT/insert-after are handed back untouched */
    r2_cbreturn(1, 0x58, (char *)buf + 0x40, 0x18, 0);
}
R2_SCN(cbt_createwnd_nonzero_result_cancels_window_creation) {
    NEED_HOOK();
    WCHAR cls[64]; WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    _snwprintf(cls, 63, L"win32ktR2_cbtr_%lu", GetCurrentProcessId());
    wc.lpfnWndProc = cs_plain_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cls;
    R2_NEED(RegisterClassW(&wc), "no class");
    memset(&g_cbt, 0, sizeof g_cbt);
    HHOOK hk = SetWindowsHookExW(WH_CBT, cbt_proc, NULL, GetCurrentThreadId());
    R2_NEED(hk, "SetWindowsHookEx(WH_CBT) failed");
    /* probe: the normal path must use slot 42 before we start steering it */
    LONG m = r2_mark();
    HWND h0 = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"probe", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wc.hInstance, NULL);
    R2_NEED(h0, "no window");
    int used = r2_find(42, m, 0) != NULL;
    DestroyWindow(h0);
    if (!used) { KW(0 && "WH_CBT create must use slot 42"); UnhookWindowsHookEx(hk); UnregisterClassW(cls, wc.hInstance); return R2_SKIP; }
    /* documented: a nonzero HCBT_CREATEWND result destroys the window and CreateWindowEx returns NULL */
    memset(&g_cbt, 0, sizeof g_cbt);
    r2_ovr[42] = cbt_cancel_override;
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"cancelled", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wc.hInstance, NULL);
    r2_ovr[42] = NULL;
    K(h == NULL);
    KEQ(g_cbt.create_calls, 0);                         /* the user hook was replaced by our result */
    if (h) DestroyWindow(h);
    /* with the table restored the same hook lets the window through again */
    HWND h2 = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"allowed", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wc.hInstance, NULL);
    K(h2 != NULL);
    KEQ(g_cbt.create_calls, 1);
    if (h2) DestroyWindow(h2);
    UnhookWindowsHookEx(hk);
    UnregisterClassW(cls, wc.hInstance);
    return 0;
}

static struct { volatile LONG calls; int code; WPARAM wp; MSG msg; } g_gm;
static LRESULT CALLBACK getmsg_proc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && lp) {
        MSG *m = (MSG *)lp;
        if (m->message == WM_USER + 0x77) { g_gm.calls++; g_gm.code = code; g_gm.wp = wp; g_gm.msg = *m; }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}
R2_SCN(getmessage_hook_uses_slot_47_with_the_msg_in_the_buffer) {
    NEED_HOOK();
    HWND h = k_make_window("r2cb-gm");
    R2_NEED(h, "no window");
    memset(&g_gm, 0, sizeof g_gm);
    HHOOK hk = SetWindowsHookExW(WH_GETMESSAGE, getmsg_proc, NULL, GetCurrentThreadId());
    R2_NEED(hk, "SetWindowsHookEx(WH_GETMESSAGE) failed");
    LONG m = r2_mark();
    PostMessageW(h, WM_USER + 0x77, 0x1122334455667788ULL, 0x0A0B0C0D0E0F1011LL);
    for (int i = 0; i < 20 && g_gm.calls == 0; i++) k_pump(25);
    KEQ(g_gm.calls, 1);
    KEQ(g_gm.wp, PM_REMOVE);
    KEQ(g_gm.msg.hwnd, h);
    KEQ(g_gm.msg.wParam, 0x1122334455667788ULL);
    KEQ(g_gm.msg.lParam, 0x0A0B0C0D0E0F1011LL);
    struct r2_ev *e = NULL;
    for (int i = 0; ; i++) { struct r2_ev *x = r2_find(47, m, i); if (!x) break; unsigned long long v = 0x1122334455667788ULL; if (find_bytes(x->data, x->ncopy, &v, 8)) { e = x; break; } }
    KW(e != NULL);
    if (e) { KEQ(e->len, 0x58); KEQ(e->tid, GetCurrentThreadId()); }
    UnhookWindowsHookEx(hk);
    DestroyWindow(h);
    return 0;
}
