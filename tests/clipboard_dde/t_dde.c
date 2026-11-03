/* clipboard_dde: DDE instance objects and DDE client impersonation.
 *
 * user32!InternalDdeInitialize creates a hidden client window, then NtUserDdeInitialize ->
 * the kernel creates a monitor window (a system class) and a
 * TYPE_DDEACCESS (9) object, returning the object's handle, the window handle and the current
 * monitor flags. DdeUninitialize calls NtUserCallOneParam(hInst, 0x2f) = _CsDdeUninitialize ->
 * the DDE object teardown, which only acts when the object is type 9, not already destroyed and
 * owned by the calling thread. NtUserUpdateInstance(hInst, &flags, newFlags) answers 0x4006
 * (DMLERR_INVALIDPARAMETER) for anything that is not a live type 9 handle.
 * NtUserImpersonateDdeClientWindow(client, server): both windows validated, the server must
 * belong to the calling thread (else ERROR_INVALID_PARAMETER), same-process pairs return TRUE. */
#include "cd.h"
#include <ddeml.h>

UDECL(DWORD, NtUserDdeInitialize, (ULONG_PTR *, HWND *, DWORD *, DWORD, ULONG_PTR));
UDECL(DWORD, NtUserUpdateInstance, (ULONG_PTR, DWORD *, DWORD));
UDECL(BOOL,  NtUserImpersonateDdeClientWindow, (HWND, HWND));
UDECL(BOOL,  ImpersonateDdeClientWindow, (HWND, HWND));

#define CD_TYPE_DDEACCESS 9

/* stands in for the client-side tagCL_INSTANCE_INFO pointer that user32 passes as the last argument;
 * the kernel only stores it, but keep it dereferenceable */
static char s_fake_cii[0x80];

static HDDEDATA CALLBACK dde_cb(UINT t, UINT f, HCONV c, HSZ h1, HSZ h2, HDDEDATA d, ULONG_PTR x1, ULONG_PTR x2) {
    (void)t; (void)f; (void)c; (void)h1; (void)h2; (void)d; (void)x1; (void)x2;
    return 0;
}

struct wcount { int n; char names[256]; };
static BOOL CALLBACK count_cb(HWND h, LPARAM l) {
    struct wcount *w = (struct wcount *)l;
    char cls[64] = "?";
    U(GetClassNameA)(h, cls, sizeof cls);
    if (strlen(w->names) + strlen(cls) + 2 < sizeof w->names) { if (w->n) strcat(w->names, ","); strcat(w->names, cls); }
    w->n++;
    return TRUE;
}
static int thread_windows(struct wcount *w) {
    struct wcount tmp; memset(&tmp, 0, sizeof tmp);
    U(EnumThreadWindows)(GetCurrentThreadId(), count_cb, (LPARAM)&tmp);
    if (w) *w = tmp;
    return tmp.n;
}

TEST(clipboard_dde_dde_initialize_uninitialize_cycle) {
    CD_SETUP();
    REQUIRE(U(DdeInitializeW) && U(DdeUninitialize) && U(EnumThreadWindows) && U(GetClassNameA));
    U(GetDesktopWindow)();
    struct wcount a, b;
    int w0 = thread_windows(&a);
    DWORD id = 0;
    EQ(U(DdeInitializeW)(&id, dde_cb, APPCLASS_STANDARD, 0), DMLERR_NO_ERROR);
    CHECK(id != 0);
    int w1 = thread_windows(&b);
    CHECK(w1 >= w0 + 1);                                    /* the client DDEML window lives on this thread */
    t_note("thread windows %d -> %d after DdeInitialize: [%s]", w0, w1, b.names);
    CHECK(U(DdeUninitialize)(id));
    EQ(thread_windows(NULL), w0);                    /* every window created for the instance is gone again */
    SetLastError(0);
    CHECK(!U(DdeUninitialize)(id));                         /* ValidateInstance fails the second time */
}

TEST(clipboard_dde_dde_initialize_reserved_must_be_zero) {
    CD_SETUP();
    REQUIRE(U(DdeInitializeW));
    DWORD id = 0;
    EQ(U(DdeInitializeW)(&id, dde_cb, APPCLASS_STANDARD, 1), DMLERR_INVALIDPARAMETER);   /* checked before anything is created */
    EQ(id, 0);
    EQ(U(DdeInitializeW)(&id, dde_cb, APPCLASS_STANDARD, 0xFFFF0000), DMLERR_INVALIDPARAMETER);
    EQ(id, 0);
}

TEST(clipboard_dde_dde_initialize_unknown_instance_id) {
    CD_SETUP();
    REQUIRE(U(DdeInitializeW));
    DWORD id = 0x12345;
    EQ(U(DdeInitializeW)(&id, dde_cb, APPCLASS_STANDARD, 0), DMLERR_INVALIDPARAMETER);   /* *pidInst != 0 means "re-initialize" */
    EQ(id, 0x12345);
}

TEST(clipboard_dde_dde_initialize_update_keeps_instance) {
    CD_SETUP();
    REQUIRE(U(DdeInitializeW) && U(DdeUninitialize) && U(EnumThreadWindows) && U(GetClassNameA));
    U(GetDesktopWindow)();
    DWORD id = 0;
    EQ(U(DdeInitializeW)(&id, dde_cb, APPCLASS_STANDARD, 0), DMLERR_NO_ERROR);
    DWORD first = id;
    int w1 = thread_windows(NULL);
    EQ(U(DdeInitializeW)(&id, dde_cb, APPCLASS_STANDARD | CBF_SKIP_REGISTRATIONS | CBF_SKIP_UNREGISTRATIONS, 0), DMLERR_NO_ERROR);
    EQ(id, first);                                   /* NtUserUpdateInstance path: same instance */
    EQ(thread_windows(NULL), w1);                    /* nothing new is created */
    CHECK(U(DdeUninitialize)(id));
}

TEST(clipboard_dde_dde_instances_are_distinct) {
    CD_SETUP();
    REQUIRE(U(DdeInitializeW) && U(DdeUninitialize) && U(EnumThreadWindows) && U(GetClassNameA));
    U(GetDesktopWindow)();
    DWORD a = 0, b = 0, c = 0;
    int w0 = thread_windows(NULL);
    EQ(U(DdeInitializeW)(&a, dde_cb, APPCLASS_STANDARD, 0), 0);
    EQ(U(DdeInitializeW)(&b, dde_cb, APPCLASS_STANDARD, 0), 0);
    EQ(U(DdeInitializeW)(&c, dde_cb, APPCLASS_STANDARD, 0), 0);
    CHECK(a && b && c && a != b && b != c && a != c);
    CHECK(thread_windows(NULL) >= w0 + 3);
    CHECK(U(DdeUninitialize)(b));
    CHECK(U(DdeUninitialize)(a));
    CHECK(U(DdeUninitialize)(c));
    EQ(thread_windows(NULL), w0);
}

/* ---- direct syscalls ------------------------------------------------------------------------ */
static void dde_direct_cleanup(ULONG_PTR inst, HWND wnd) {
    NtUserCallOneParam_pfn c1 = UN(NtUserCallOneParam);
    if (inst && c1) c1(inst, 0x2F);
    if (wnd && U(IsWindow)(wnd)) U(DestroyWindow)(wnd);
}

TEST(clipboard_dde_ddeinit_direct_returns_type9_handle) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserDdeInitialize_pfn init = UN(NtUserDdeInitialize);
    NtUserCallOneParam_pfn c1 = UN(NtUserCallOneParam);
    NtUserUpdateInstance_pfn upd = UN(NtUserUpdateInstance);
    REQUIRE(init && c1 && upd);
    U(GetDesktopWindow)();
    ULONG_PTR inst = 0; HWND w = NULL; DWORD mf = 0x55;
    EQ(init(&inst, &w, &mf, 0x20, (ULONG_PTR)s_fake_cii), 0);
    CHECK(inst != 0);
    CHECK(w != NULL);
    struct k_user_entry e;
    if (inst && k_user_entry((HANDLE)inst, &e)) {
        EQ(e.type, CD_TYPE_DDEACCESS);
        EQ(e.uniq, (unsigned short)(inst >> 16));
        CHECK(!(e.flags & 1));
    } else t_note("handle table entry for instance %p unreadable", (void *)inst);
    if (w) {
        CHECK(U(IsWindow)(w));
        DWORD pid = 0;
        EQ(U(GetWindowThreadProcessId)(w, &pid), GetCurrentThreadId());   /* created on the calling thread */
        EQ(pid, GetCurrentProcessId());
        char cls[64] = {0};
        U(GetClassNameA)(w, cls, sizeof cls);
        t_note("monitor flags 0x%lx, kernel DDE window class \"%s\"", mf, cls);
    }
    DWORD out = 0x77;
    EQ(upd(inst, &out, 0x20), 0);                    /* live type 9 handle */
    EQ((ULONG_PTR)c1(inst, 0x2F), 1);                /* _CsDdeUninitialize always answers 1 */
    out = 0x77;
    EQ(upd(inst, &out, 0x20), DMLERR_INVALIDPARAMETER);   /* destroyed now */
    EQ(out, 0);
    if (w && U(IsWindow)(w)) { t_note("instance window survived the uninitialize"); }
    dde_direct_cleanup(0, w);
}

TEST(clipboard_dde_ddeinit_direct_two_instances_differ) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserDdeInitialize_pfn init = UN(NtUserDdeInitialize);
    REQUIRE(init && UN(NtUserCallOneParam));
    U(GetDesktopWindow)();
    ULONG_PTR i1 = 0, i2 = 0; HWND w1 = NULL, w2 = NULL; DWORD m1, m2;
    EQ(init(&i1, &w1, &m1, 0x20, (ULONG_PTR)s_fake_cii), 0);
    EQ(init(&i2, &w2, &m2, 0x20, (ULONG_PTR)s_fake_cii), 0);
    CHECK(i1 && i2 && i1 != i2);
    CHECK(w1 && w2 && w1 != w2);
    dde_direct_cleanup(i1, w1);
    dde_direct_cleanup(i2, w2);
}

TEST(clipboard_dde_updateinstance_direct_rejects_other_handles) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserUpdateInstance_pfn upd = UN(NtUserUpdateInstance);
    REQUIRE(upd);
    struct cd_ctx *c = cd_new();
    ULONG_PTR bad[] = { 0, 0x7FFE7777, 0xFFFFFFFF, c ? (ULONG_PTR)c->hwnd : 0 };   /* a window is type 1, not 9 */
    for (int i = 0; i < 4; i++) {
        DWORD out = 0x55;
        CHECKF(upd(bad[i], &out, 0x20) == DMLERR_INVALIDPARAMETER, "handle %p", (void *)bad[i]);
        CHECKF(out == 0, "handle %p out=%lu", (void *)bad[i], out);
    }
}

static DWORD WINAPI thr_uninit(LPVOID p) {
    NtUserCallOneParam_pfn c1 = UN(NtUserCallOneParam);
    return (DWORD)c1((ULONG_PTR)p, 0x2F);
}

TEST(clipboard_dde_uninit_from_other_thread_is_noop) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserDdeInitialize_pfn init = UN(NtUserDdeInitialize);
    NtUserUpdateInstance_pfn upd = UN(NtUserUpdateInstance);
    REQUIRE(init && upd && UN(NtUserCallOneParam));
    U(GetDesktopWindow)();
    ULONG_PTR inst = 0; HWND w = NULL; DWORD mf;
    EQ(init(&inst, &w, &mf, 0x20, (ULONG_PTR)s_fake_cii), 0);
    EQ(cd_thread(thr_uninit, (void *)inst), 1);       /* answers 1 either way... */
    DWORD out = 0;
    EQ(upd(inst, &out, 0x20), 0);                     /* ...but the teardown refused: owner thread differs */
    struct k_user_entry e;
    if (k_user_entry((HANDLE)inst, &e)) EQ(e.type, CD_TYPE_DDEACCESS);
    dde_direct_cleanup(inst, w);
}

TEST(clipboard_dde_uninit_invalid_handle_answers_one) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserCallOneParam_pfn c1 = UN(NtUserCallOneParam);
    REQUIRE(c1);
    U(GetDesktopWindow)();
    EQ((ULONG_PTR)c1(0, 0x2F), 1);
    EQ((ULONG_PTR)c1(0x7FFE7777, 0x2F), 1);
    EQ((ULONG_PTR)c1((ULONG_PTR)U(GetDesktopWindow)(), 0x2F), 1);   /* a window handle: type check fails, nothing destroyed */
    CHECK(U(IsWindow)(U(GetDesktopWindow)()));
}

/* ---- ImpersonateDdeClientWindow ------------------------------------------------------------------ */
struct imp_helper { HANDLE ready, done; HWND hwnd; };
static DWORD WINAPI imp_thread(LPVOID p) {
    struct imp_helper *h = p;
    struct cd_ctx *c = cd_new();
    h->hwnd = c ? c->hwnd : NULL;
    SetEvent(h->ready);
    WaitForSingleObject(h->done, 10000);
    if (c) { k_pump(5); cd_destroy(c); }
    return 0;
}

TEST(clipboard_dde_impersonate_dde_client_validation) {
    CD_SETUP();
    REQUIRE(UN(ImpersonateDdeClientWindow));
    struct cd_ctx *a = cd_new(), *b = cd_new();
    if (!a || !b) { t_skip("no window"); return; }
    HWND bad = (HWND)(ULONG_PTR)0x7FFE7777;
    SetLastError(0xDEAD);
    CHECK(!UN(ImpersonateDdeClientWindow)(bad, a->hwnd));  EXPECT_ERR(CD_ERR_INVALID_WINDOW);
    SetLastError(0xDEAD);
    CHECK(!UN(ImpersonateDdeClientWindow)(a->hwnd, bad));  EXPECT_ERR(CD_ERR_INVALID_WINDOW);
    SetLastError(0xDEAD);
    CHECK(!UN(ImpersonateDdeClientWindow)(NULL, a->hwnd)); EXPECT_ERR(CD_ERR_INVALID_WINDOW);
    SetLastError(0xDEAD);
    CHECK(!UN(ImpersonateDdeClientWindow)(a->hwnd, NULL)); EXPECT_ERR(CD_ERR_INVALID_WINDOW);
    CHECK(UN(ImpersonateDdeClientWindow)(a->hwnd, b->hwnd));    /* same process: succeeds without impersonating anything */
    CHECK(UN(ImpersonateDdeClientWindow)(a->hwnd, a->hwnd));
}

TEST(clipboard_dde_impersonate_dde_server_must_be_current_thread) {
    CD_SETUP();
    REQUIRE(UN(ImpersonateDdeClientWindow) && U(GetDesktopWindow));
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    HWND desk = U(GetDesktopWindow)();
    SetLastError(0xDEAD);
    CHECK(!UN(ImpersonateDdeClientWindow)(a->hwnd, desk));  /* hWndServer is owned by another thread => ERROR_INVALID_PARAMETER */
    EXPECT_ERR(CD_ERR_INVALID_PARAMETER);
}

TEST(clipboard_dde_impersonate_dde_server_on_other_thread_same_process) {
    CD_SETUP();
    REQUIRE(UN(ImpersonateDdeClientWindow));
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    struct imp_helper h = { CreateEventW(NULL, TRUE, FALSE, NULL), CreateEventW(NULL, TRUE, FALSE, NULL), NULL };
    HANDLE t = CreateThread(NULL, 0, imp_thread, &h, 0, NULL);
    if (!t) { t_skip("thread"); return; }
    WaitForSingleObject(h.ready, 5000);
    if (h.hwnd) {
        CHECK(UN(ImpersonateDdeClientWindow)(h.hwnd, a->hwnd));    /* client on another thread of this process: same ppi => TRUE */
        SetLastError(0xDEAD);
        CHECK(!UN(ImpersonateDdeClientWindow)(a->hwnd, h.hwnd));   /* server must be a window of the calling thread */
        EXPECT_ERR(CD_ERR_INVALID_PARAMETER);
    } else t_note("helper thread could not create a window");
    SetEvent(h.done);
    WaitForSingleObject(t, 5000);
    CloseHandle(t); CloseHandle(h.ready); CloseHandle(h.done);
}

TEST(clipboard_dde_impersonate_dde_direct_matches_user32) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserImpersonateDdeClientWindow_pfn f = UN(NtUserImpersonateDdeClientWindow);
    REQUIRE(f);
    struct cd_ctx *a = cd_new(), *b = cd_new();
    if (!a || !b) { t_skip("no window"); return; }
    CHECK(f(a->hwnd, b->hwnd));
    SetLastError(0xDEAD);
    CHECK(!f((HWND)(ULONG_PTR)0x7FFE7777, b->hwnd));
    EXPECT_ERR(CD_ERR_INVALID_WINDOW);
    SetLastError(0xDEAD);
    CHECK(!f(a->hwnd, U(GetDesktopWindow)()));
    EXPECT_ERR(CD_ERR_INVALID_PARAMETER);
}
