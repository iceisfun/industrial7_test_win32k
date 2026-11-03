/* r2_wnd_cls_heap_fields: tagDESKTOPINFO (TEB.Win32ClientInfo.pDeskInfo, TEB+0x820), the desktop window
 * and the per-desktop view of the heap.
 *
 * Offsets (19041 user32): +0x00 base value compared with every handle-table entry's desktop guard,
 * +0x08 desktop window (GetCurrentThreadDesktopWindow), +0x10 DWORD fsHooks (SendMessageWorker),
 * +0x18 shell (GetShellWindow), +0x20 taskman (GetTaskmanWindow), +0x28 progman (GetProgmanWindow); the
 * window pointers are kernel addresses and user32 adds ulClientDelta before reading the HWND at +0. */
#define R2H_TAG L"di"
#include "r2h.h"

TEST(r2_wnd_cls_heap_fields_di_spwnd_is_desktop_window) {
    NEED_GUI();
    uint64_t di = deskinfo_now();
    HWND d = U(GetDesktopWindow)();
    CHECK(d != NULL);
    EXPECT_EQ(kptr_hwnd(read_q(di + DI_SPWND)), d);
    END();
}
TEST(r2_wnd_cls_heap_fields_di_spwnd_object_view) {
    NEED_GUI();
    HWND d = U(GetDesktopWindow)();
    NEED_WND(d, w);
    uint64_t di = deskinfo_now(), k = read_q(di + DI_SPWND);
    EXPECT_EQ(k, W64(&w, W_PSELF));                    /* the stored pointer is the object's own pSelf */
    EXPECT_EQ(w.pc, k + delta_now());
    EXPECT_EQ(w.e.phead, k);
    END();
}
TEST(r2_wnd_cls_heap_fields_di_desktop_window_class_and_fnid) {
    NEED_GUI();
    HWND d = U(GetDesktopWindow)();
    NEED_WND(d, w);
    EXPECT_EQ(wv_fnid(&w), FNID_DESKTOP);              /* GetWindowRect & co. special-case (fnid & 0x2fff) == 0x29d */
    wchar_t cn[64] = L"";
    CHECK(U(GetClassNameW)(d, cn, 64) > 0);
    EXPECT_WSTR(cn, L"#32769");
    EXPECT_EQ(W64(&w, W_PARENT), 0);
    EXPECT_EQ(U(GetParent)(d), NULL);
    EXPECT_EQ(U(GetWindow)(d, GW_OWNER), NULL);
    CHECK(U(IsWindowVisible)(d));
    END();
}
TEST(r2_wnd_cls_heap_fields_di_desktop_window_class_atom_matches_api) {
    NEED_GUI();
    HWND d = U(GetDesktopWindow)();
    NEED_WND(d, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    EXPECT_EQ((ULONG_PTR)U(GetClassWord)(d, GCW_ATOM), C16(&c, C_ATOM));
    EXPECT_EQ(C16(&c, C_ATOM) != 0, 1);
    END();
}
TEST(r2_wnd_cls_heap_fields_di_shell_matches_api) {
    NEED_GUI();
    uint64_t di = deskinfo_now();
    HWND api = U(GetShellWindow)();
    EXPECT_EQ(kptr_hwnd(read_q(di + DI_SHELL)), api);
    if (!api) t_note("no shell window on this desktop");
    else {
        CHECK(U(IsWindow)(api));
        DWORD pid = 0;
        CHECK(U(GetWindowThreadProcessId)(api, &pid) != 0);
        CHECK(pid != GetCurrentProcessId());
        struct wv w;
        if (wv_get(api, &w)) EXPECT_EQ(W64(&w, W_PARENT) != 0, 1);   /* a top-level window: parent is the desktop */
    }
    END();
}
UDECL(HWND, GetProgmanWindow, (void));
UDECL(HWND, GetTaskmanWindow, (void));
TEST(r2_wnd_cls_heap_fields_di_progman_matches_api) {
    NEED_GUI();
    REQUIRE(UN(GetProgmanWindow));
    uint64_t di = deskinfo_now();
    HWND api = UN(GetProgmanWindow)();
    EXPECT_EQ(kptr_hwnd(read_q(di + DI_PROGMAN)), api);
    if (api) CHECK(U(IsWindow)(api)); else t_note("no progman window on this desktop");
    END();
}
TEST(r2_wnd_cls_heap_fields_di_taskman_matches_api) {
    NEED_GUI();
    REQUIRE(UN(GetTaskmanWindow));
    uint64_t di = deskinfo_now();
    HWND api = UN(GetTaskmanWindow)();
    EXPECT_EQ(kptr_hwnd(read_q(di + DI_TASKMAN)), api);
    if (api) CHECK(U(IsWindow)(api)); else t_note("no taskman window on this desktop");
    END();
}
TEST(r2_wnd_cls_heap_fields_di_shell_window_not_ours_after_create) {
    NEED_GUI();
    HWND before = U(GetShellWindow)();
    HWND h = mk_plain();
    NEED_WND(h, w);
    EXPECT_EQ(U(GetShellWindow)(), before);            /* creating windows never changes the shell designation */
    CHECK(U(GetShellWindow)() != h);
    END();
}
TEST(r2_wnd_cls_heap_fields_di_base_is_window_guard_for_every_window) {
    NEED_GUI();
    HWND h = mk_plain(), host = mk_host(), c = mk_child(host, plain_class(), WS_CHILD, 0, 0, 5, 5, 1);
    uint64_t base = read_q(deskinfo_now() + DI_BASE);
    CHECK(base != 0);
    HWND all[] = { h, host, c, U(GetDesktopWindow)() };
    for (size_t i = 0; i < 4; i++) {
        struct wv w;
        if (!wv_get(all[i], &w)) { t_note("window %zu not decodable", i); continue; }
        EXPECT_EQ(w.e.guard, base);
    }
    END();
}
TEST(r2_wnd_cls_heap_fields_di_window_objects_share_mapping_with_deskinfo) {
    NEED_GUI();
    HWND h = mk_plain();
    NEED_WND(h, w);
    uint64_t di = deskinfo_now();
    MEMORY_BASIC_INFORMATION a, b;
    CHECK(vq(di, &a));
    CHECK(vq(w.pc, &b));
    CHECK(a.AllocationBase == b.AllocationBase);      /* DESKTOPINFO lives in the same desktop heap view */
    CHECK(a.Type == MEM_MAPPED);
    END();
}
TEST(r2_wnd_cls_heap_fields_di_heap_is_read_only_for_user_mode) {
    NEED_GUI();
    HWND h = mk_plain();
    NEED_WND(h, w);
    MEMORY_BASIC_INFORMATION m;
    CHECK(vq(w.pc, &m));
    CHECK(m.Protect == PAGE_READONLY);                 /* the client view is mapped read-only */
    CHECK(vq(deskinfo_now(), &m));
    CHECK(m.Protect == PAGE_READONLY);
    END();
}
TEST(r2_wnd_cls_heap_fields_di_toplevel_windows_listed_under_desktop_child_chain) {
    NEED_GUI();
    HWND h = mk_plain();
    NEED_WND(h, w);
    HWND d = U(GetDesktopWindow)();
    struct wv wd;
    CHECK(wv_get(d, &wd));
    int found = 0;
    HWND cur = wv_link(&wd, W_CHILD);
    for (int i = 0; i < 4000 && cur && cur != (HWND)(ULONG_PTR)-1; i++) {
        if (cur == h) { found = 1; break; }
        struct wv wc;
        if (!wv_get(cur, &wc)) break;
        cur = wv_link(&wc, W_NEXT);
    }
    CHECK(found);
    END();
}

/* ---- a second desktop in a child process: separate DESKTOPINFO, heap, system classes -------------- */
static int g_cf;
#define CCHK(c) do { if (!(c)) { g_cf++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

CHILD(r2h_newdesk) {
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!U(CreateDesktopW) || !U(SetThreadDesktop) || !U(GetThreadDesktop) || !U(CreateWindowExW)) return 77;
    HWND d0 = U(GetDesktopWindow)();
    uint64_t di0 = deskinfo_now(), base0 = read_q(di0 + DI_BASE), delta0 = delta_now();
    if (!di0 || !d0) return 77;
    wchar_t bn[64];
    _snwprintf(bn, 64, L"win32kt_r2h_%lu", GetCurrentProcessId());
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = r2h_wp2; wc.hInstance = g_hinst(); wc.lpszClassName = bn;
    if (!U(RegisterClassW)(&wc)) return 77;
    /* a system class window on the original desktop */
    HWND b0 = U(CreateWindowExW)(EX_GUEST, L"Button", L"b", WS_POPUP, -30000, -30000, 20, 20, NULL, NULL, g_hinst(), NULL);
    struct wv wb0; struct cv cb0;
    bool have0 = b0 && wv_get(b0, &wb0) && cv_get(&wb0, &cb0);
    HWND own0 = U(CreateWindowExW)(EX_GUEST, bn, L"o", WS_POPUP, -30000, -30000, 20, 20, NULL, NULL, g_hinst(), NULL);
    struct wv wo0; struct cv co0;
    bool haveo0 = own0 && wv_get(own0, &wo0) && cv_get(&wo0, &co0);
    /* a thread that owns windows cannot change desktop: use a helper thread for the second desktop */
    HDESK hd = U(CreateDesktopW)(bn, NULL, NULL, 0, GENERIC_ALL, NULL);
    if (!hd) { printf("  NOTE CreateDesktop failed %lu\n", GetLastError()); return 77; }
    struct { HDESK hd; int cf; } *unused = NULL; (void)unused;
    if (own0) U(DestroyWindow)(own0);
    if (b0) U(DestroyWindow)(b0);
    HDESK old = U(GetThreadDesktop)(GetCurrentThreadId());
    if (!U(SetThreadDesktop)(hd)) { printf("  NOTE SetThreadDesktop failed %lu\n", GetLastError()); U(CloseDesktop)(hd); return 77; }
    U(GetDesktopWindow)();
    uint64_t di1 = deskinfo_now(), delta1 = delta_now();
    HWND d1 = U(GetDesktopWindow)();
    CCHK(di1 != 0 && d1 != NULL);
    CCHK(di1 != di0);                                  /* each desktop has its own DESKTOPINFO */
    CCHK(d1 != d0);
    CCHK(kptr_hwnd(read_q(di1 + DI_SPWND)) == d1);
    uint64_t base1 = read_q(di1 + DI_BASE);
    CCHK(base1 != base0);
    CCHK(U(GetShellWindow)() == NULL);                 /* nobody registered a shell on the new desktop */
    CCHK(read_q(di1 + DI_SHELL) == 0);
    CCHK(read_q(di1 + DI_PROGMAN) == 0 && read_q(di1 + DI_TASKMAN) == 0);
    struct wv wd1;
    CCHK(wv_get(d1, &wd1));
    CCHK(wd1.e.guard == base1);
    CCHK(wv_fnid(&wd1) == FNID_DESKTOP);
    /* windows created now live in the new desktop heap and carry its guard */
    HWND own1 = U(CreateWindowExW)(EX_GUEST, bn, L"o", WS_POPUP, -30000, -30000, 20, 20, NULL, NULL, g_hinst(), NULL);
    struct wv wo1; struct cv co1;
    CCHK(own1 && wv_get(own1, &wo1));
    if (own1 && wv_get(own1, &wo1)) {
        CCHK(wo1.e.guard == base1);
        CCHK(wo1.e.guard != base0);
        CCHK(W64(&wo1, W_PARENT) != 0);
        CCHK(wv_link(&wo1, W_PARENT) == d1);           /* top-level windows hang off this desktop's window */
        CCHK(!same_view(wo1.pc, haveo0 ? wo0.pc : 0) || !haveo0);
        if (cv_get(&wo1, &co1) && haveo0)
            CCHK(C16(&co1, C_ATOM) == C16(&co0, C_ATOM));   /* an application class is one atom for the process */
    }
    HWND b1 = U(CreateWindowExW)(EX_GUEST, L"Button", L"b", WS_POPUP, -30000, -30000, 20, 20, NULL, NULL, g_hinst(), NULL);
    struct wv wb1; struct cv cb1;
    if (b1 && wv_get(b1, &wb1) && cv_get(&wb1, &cb1) && have0) {
        CCHK(C16(&cb1, C_ATOM) == C16(&cb0, C_ATOM));   /* same system class atom ... */
        CCHK(!same_view(cb1.pc, cb0.pc));               /* ... but a per-desktop class object in the other heap */
        CCHK(wv_fnid(&wb1) == wv_fnid(&wb0));
    } else if (b1) { printf("  NOTE button views not decodable on second desktop\n"); }
    if (own1) U(DestroyWindow)(own1);
    if (b1) U(DestroyWindow)(b1);
    CCHK(U(SetThreadDesktop)(old));
    CCHK(deskinfo_now() == di0);
    CCHK(U(GetDesktopWindow)() == d0);
    (void)delta0; (void)delta1;
    U(CloseDesktop)(hd);
    return g_cf > 250 ? 250 : g_cf;
}
TEST(r2_wnd_cls_heap_fields_di_second_desktop_separate_deskinfo_and_classes) {
    NEED_GUI();
    if (t_is_wine()) { t_skip("needs real win32k desktops"); return; }
    struct k_child_result r = k_child_run("r2h_newdesk", "", 30000);
    if (r.exit_code == 77) { t_skip("child could not create or enter a second desktop"); return; }
    EXPECT_EQ(r.exit_code, 0);
    END();
}
