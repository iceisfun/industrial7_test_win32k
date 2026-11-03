/* clipboard_dde: WM_RENDERFORMAT / WM_RENDERALLFORMATS lifecycle of a delayed-rendering owner.
 *
 * The owner window here follows the documented protocol exactly: on WM_RENDERFORMAT it renders the one
 * requested format with SetClipboardData (the clipboard is already open by the requester, so it does not open or
 * empty); on WM_RENDERALLFORMATS it first opens the clipboard, then verifies GetClipboardOwner() == itself,
 * renders only the formats that are still delayed, never calls EmptyClipboard, and closes. Scenarios:
 *   - a request for one delayed format renders exactly that format and never sends WM_RENDERALLFORMATS;
 *   - owner destroyed with one immediate and one delayed format: the immediate one stays, the delayed one is rendered;
 *   - another process replaces the clipboard before the owner dies: the old owner is no longer the clipboard owner,
 *     so a conforming handler renders nothing and the new content is untouched;
 *   - an owner process that destroys its window and exits leaves rendered data behind. */
#include "cd.h"

static int cf;
#define CH(c) do { if (!(c)) { cf++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define RL_MAXF 8
struct rl {
    HWND hwnd;
    UINT fmt[RL_MAXF];               /* formats the owner can render */
    const char *data[RL_MAXF];
    int  rendered[RL_MAXF];          /* the owner called SetClipboardData for it */
    int  nfmt;
    int  n_renderfmt;
    UINT req[16];                    /* WM_RENDERFORMAT wParam values in arrival order */
    int  n_all;                      /* WM_RENDERALLFORMATS count */
    int  n_destroycb;                /* WM_DESTROYCLIPBOARD count */
    int  all_open_ok;
    int  all_owner_is_me;            /* GetClipboardOwner()==hwnd inside WM_RENDERALLFORMATS, after OpenClipboard */
    int  all_set_ok;
    int  all_did_render;
    int  protocol;                   /* 1: conforming handler for WM_RENDERALLFORMATS */
    WPARAM all_w; LPARAM all_l;
};

static WCHAR s_cls[64];

static LRESULT CALLBACK rl_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    struct rl *r;
    if (m == WM_NCCREATE) {
        U(SetWindowLongPtrW)(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams);
        return U(DefWindowProcW)(h, m, w, l);
    }
    r = (struct rl *)U(GetWindowLongPtrW)(h, GWLP_USERDATA);
    if (!r) return U(DefWindowProcW)(h, m, w, l);
    switch (m) {
    case CD_WM_RENDERFORMAT: {
        int i;
        if (r->n_renderfmt < 16) r->req[r->n_renderfmt] = (UINT)w;
        r->n_renderfmt++;
        for (i = 0; i < r->nfmt; i++) if (r->fmt[i] == (UINT)w) {
            HGLOBAL g = cd_gstr_a(r->data[i]);
            if (g && U(SetClipboardData)((UINT)w, g)) r->rendered[i] = 1;
        }
        return 0;
    }
    case CD_WM_RENDERALLFORMATS:
        r->n_all++; r->all_w = w; r->all_l = l;
        if (r->protocol && U(OpenClipboard)(h)) {
            r->all_open_ok = 1;
            r->all_owner_is_me = (U(GetClipboardOwner)() == h);
            if (r->all_owner_is_me) {
                int i;
                for (i = 0; i < r->nfmt; i++) {
                    HGLOBAL g;
                    if (r->rendered[i]) continue;
                    g = cd_gstr_a(r->data[i]);
                    r->all_did_render = 1;
                    if (g && U(SetClipboardData)(r->fmt[i], g)) { r->rendered[i] = 1; r->all_set_ok++; }
                }
            }
            U(CloseClipboard)();
        }
        return 0;
    case CD_WM_DESTROYCLIPBOARD:
        r->n_destroycb++;
        return 0;
    }
    return U(DefWindowProcW)(h, m, w, l);
}

static int rl_new(struct rl *r) {
    WNDCLASSEXW wc;
    if (!s_cls[0]) {
        memset(&wc, 0, sizeof wc);
        wsprintfW(s_cls, L"win32kt_rl_%lu", GetCurrentProcessId());
        wc.cbSize = sizeof wc; wc.lpfnWndProc = rl_wndproc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = s_cls;
        if (!U(RegisterClassExW)(&wc)) { s_cls[0] = 0; return 0; }
    }
    r->hwnd = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, s_cls, L"rl", WS_POPUP, -32000, -32000, 1, 1,
                                 NULL, NULL, GetModuleHandleW(NULL), r);
    return r->hwnd != NULL;
}
static void rl_end(struct rl *r) { if (r->hwnd && U(IsWindow)(r->hwnd)) U(DestroyWindow)(r->hwnd); r->hwnd = NULL; }

/* owner puts: fmt[0] immediate (when imm), the remaining formats delayed */
static int rl_put(struct rl *r, int imm) {
    int i;
    if (!cd_open_empty(r->hwnd)) return 0;
    for (i = 0; i < r->nfmt; i++) {
        if (i == 0 && imm) { if (!U(SetClipboardData)(r->fmt[0], cd_gstr_a(r->data[0]))) { U(CloseClipboard)(); return 0; } r->rendered[0] = 1; }
        else U(SetClipboardData)(r->fmt[i], NULL);
    }
    U(CloseClipboard)();
    return 1;
}
static int get_str(UINT fmt, char *out, size_t cap) {
    HANDLE h = U(GetClipboardData)(fmt);
    char *p;
    if (!h) return 0;
    p = GlobalLock(h);
    if (!p) return 0;
    snprintf(out, cap, "%s", p);
    GlobalUnlock(h);
    return 1;
}
static void rl_setup(struct rl *r, int n, const char *tag) {
    static const char *names[] = { "imm", "d1", "d2", "d3" };
    char t[64];
    memset(r, 0, sizeof *r);
    r->nfmt = n;
    for (int i = 0; i < n; i++) {
        snprintf(t, sizeof t, "rl_%s_%s", tag, names[i]);
        r->fmt[i] = cd_reg(t);
    }
    r->data[0] = "IMMEDIATE-DATA"; r->data[1] = "DELAYED-ONE"; r->data[2] = "DELAYED-TWO"; r->data[3] = "DELAYED-THREE";
    r->protocol = 1;
}

/* ---- one delayed format requested: WM_RENDERFORMAT for that format only ----------------------- */
TEST(clipboard_dde_renderlife_request_renders_only_that_format) {
    CD_SETUP();
    struct rl r;
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    rl_setup(&r, 4, "req");
    if (!rl_new(&r)) { t_skip("no window"); return; }
    CHECK(rl_put(&r, 1));
    EQ(r.n_renderfmt, 0);                                   /* nothing is rendered when the data is announced */
    CHECK(cd_open(NULL));
    char s[32] = {0};
    CHECK(get_str(r.fmt[0], s, sizeof s));                  /* the immediate format needs no message */
    CHECK(!strcmp(s, "IMMEDIATE-DATA"));
    EQ(r.n_renderfmt, 0);
    CHECK(get_str(r.fmt[2], s, sizeof s));                  /* ask for the middle delayed format */
    CHECK(!strcmp(s, "DELAYED-TWO"));
    EQ(r.n_renderfmt, 1);
    EQ(r.req[0], r.fmt[2]);
    EQ(r.n_all, 0);                                         /* the consumer's request never means "render all" */
    CHECK(U(CloseClipboard)());
    EQ(r.n_renderfmt, 1);
    EQ(r.n_all, 0);
    EQ(r.n_destroycb, 0);
    EQ(U(GetClipboardOwner)(), r.hwnd);
    rl_end(&r);
}

/* ---- owner destroyed: one immediate and one delayed format ------------------------------------- */
TEST(clipboard_dde_renderlife_destroy_renders_delayed_keeps_immediate) {
    CD_SETUP();
    struct rl r;
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    rl_setup(&r, 2, "dst");
    if (!rl_new(&r)) { t_skip("no window"); return; }
    CHECK(rl_put(&r, 1));
    EQ(r.n_renderfmt, 0);
    DWORD seq = U(GetClipboardSequenceNumber)();
    EQ(U(GetClipboardOwner)(), r.hwnd);
    HWND h = r.hwnd;
    rl_end(&r);                                             /* owner window destroyed with a delayed format pending */
    EQ(r.n_all, 1);
    EQ(r.all_w, 0);
    EQ(r.all_l, 0);
    CHECK(r.all_open_ok);                                   /* the owner could open the clipboard inside the message */
    CHECK(r.all_owner_is_me);                               /* and was still the clipboard owner there */
    EQ(r.all_set_ok, 1);                                    /* only the delayed format needed rendering */
    EQ(r.n_renderfmt, 0);                                   /* render-all does not go through WM_RENDERFORMAT */
    EQ(r.n_destroycb, 0);                                   /* nobody emptied the clipboard */
    CHECK(U(GetClipboardOwner)() != h);
    EQ(U(GetClipboardOwner)(), NULL);
    CHECK(U(GetClipboardSequenceNumber)() != seq);          /* rendering changed the content */
    CHECK(cd_open(NULL));
    char s[32] = {0};
    EQ(U(CountClipboardFormats)(), 2);
    CHECK(get_str(r.fmt[0], s, sizeof s));
    CHECK(!strcmp(s, "IMMEDIATE-DATA"));                    /* the immediate format stayed, unchanged */
    memset(s, 0, sizeof s);
    CHECK(get_str(r.fmt[1], s, sizeof s));
    CHECK(!strcmp(s, "DELAYED-ONE"));                       /* the delayed format was rendered */
    CHECK(U(CloseClipboard)());
}

/* ---- several delayed formats, one already rendered on request ---------------------------------- */
TEST(clipboard_dde_renderlife_destroy_after_partial_request) {
    CD_SETUP();
    struct rl r;
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    rl_setup(&r, 4, "mix");
    if (!rl_new(&r)) { t_skip("no window"); return; }
    CHECK(rl_put(&r, 1));
    CHECK(cd_open(NULL));
    char s[32] = {0};
    CHECK(get_str(r.fmt[2], s, sizeof s));
    CHECK(U(CloseClipboard)());
    EQ(r.n_renderfmt, 1);
    rl_end(&r);
    EQ(r.n_all, 1);
    CHECK(r.all_owner_is_me);
    EQ(r.all_set_ok, 2);                                    /* d1 and d3; d2 was rendered by the request */
    EQ(r.n_renderfmt, 1);
    EQ(U(CountClipboardFormats)(), 4);
    CHECK(cd_open(NULL));
    const char *want[4] = { "IMMEDIATE-DATA", "DELAYED-ONE", "DELAYED-TWO", "DELAYED-THREE" };
    for (int i = 0; i < 4; i++) {
        memset(s, 0, sizeof s);
        CHECK(get_str(r.fmt[i], s, sizeof s));
        CHECK(!strcmp(s, want[i]));
    }
    CHECK(U(CloseClipboard)());
    EQ(r.n_renderfmt, 1);                                   /* nothing is delayed any more */
}

/* ---- child: another process replaces the clipboard --------------------------------------------- */
static HANDLE spawn_async(const char *name, const char *args, PROCESS_INFORMATION *pi) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE || out == NULL) return t_spawn_child(name, args, 0, NULL, NULL, NULL, FALSE, NULL, pi);
    SetHandleInformation(out, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    return t_spawn_child(name, args, 0, NULL, out, out, TRUE, NULL, pi);
}
static DWORD wait_pump(HANDLE h, DWORD ms) {
    DWORD t0 = GetTickCount(), code = 0;
    while (GetTickCount() - t0 < ms) {
        if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) { GetExitCodeProcess(h, &code); return code; }
        k_pump(20);
    }
    TerminateProcess(h, 1);
    WaitForSingleObject(h, 3000);
    return 0xDEAD0001;
}
static UINT xfmt(DWORD ppid, const char *suffix) {
    WCHAR nm[96];
    wsprintfW(nm, L"win32kt_rl_%lu_%S", ppid, suffix);
    return U(RegisterClipboardFormatW)(nm);
}

CHILD(rl_replacer) {
    (void)argc;
    DWORD ppid = strtoul(argv[1], NULL, 10);
    UINT f = xfmt(ppid, "newcontent");
    BOOL ok = FALSE;
    for (int i = 0; i < 40 && !ok; i++) { ok = U(OpenClipboard)(NULL); if (!ok) Sleep(50); }
    CH(ok);
    CH(U(EmptyClipboard)());
    CH(U(SetClipboardData)(f, cd_gstr_a("NEW-CONTENT")) != NULL);
    CH(U(CloseClipboard)());
    return cf;
}

TEST(clipboard_dde_renderlife_replaced_by_other_process_no_overwrite) {
    CD_SETUP();
    struct rl r;
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    rl_setup(&r, 2, "rep");
    if (!rl_new(&r)) { t_skip("no window"); return; }
    CHECK(rl_put(&r, 1));
    EQ(U(GetClipboardOwner)(), r.hwnd);
    PROCESS_INFORMATION pi;
    char args[16];
    snprintf(args, sizeof args, "%lu", GetCurrentProcessId());
    HANDLE ch = spawn_async("rl_replacer", args, &pi);
    if (!ch) { rl_end(&r); t_skip("cannot spawn child"); return; }
    EQ(wait_pump(ch, 20000), 0);                            /* this thread services the cross-process WM_DESTROYCLIPBOARD */
    CloseHandle(pi.hThread); CloseHandle(ch);
    UINT fnew = xfmt(GetCurrentProcessId(), "newcontent");
    EQ(r.n_destroycb, 1);                                   /* the old owner is told its content was emptied */
    EQ(r.n_renderfmt, 0);
    EQ(r.n_all, 0);                                         /* replacing is not a render-all trigger */
    CHECK(U(GetClipboardOwner)() != r.hwnd);
    CHECK(U(IsClipboardFormatAvailable)(fnew));
    CHECK(!U(IsClipboardFormatAvailable)(r.fmt[0]));
    CHECK(!U(IsClipboardFormatAvailable)(r.fmt[1]));
    DWORD seq = U(GetClipboardSequenceNumber)();
    rl_end(&r);                                             /* the stale owner dies afterwards */
    EQ(r.n_all, 0);                                         /* a replaced owner is no longer asked to render anything */
    CHECK(!r.all_owner_is_me);                              /* a conforming handler never renders here */
    EQ(r.all_set_ok, 0);
    EQ(r.n_renderfmt, 0);
    EQ(U(GetClipboardSequenceNumber)(), seq);               /* the new content was not touched */
    CHECK(U(IsClipboardFormatAvailable)(fnew));
    CHECK(!U(IsClipboardFormatAvailable)(r.fmt[0]));
    CHECK(!U(IsClipboardFormatAvailable)(r.fmt[1]));
    EQ(U(CountClipboardFormats)(), 1);
    CHECK(cd_open(NULL));
    char s[32] = {0};
    CHECK(get_str(fnew, s, sizeof s));
    CHECK(!strcmp(s, "NEW-CONTENT"));
    CHECK(U(CloseClipboard)());
}

/* A non-conforming handler that opens the clipboard but skips the owner check would still find the
 * clipboard owned by someone else; only the check protects the new content. */
TEST(clipboard_dde_renderlife_stale_owner_open_is_not_owner) {
    CD_SETUP();
    struct rl r;
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    rl_setup(&r, 2, "stl");
    if (!rl_new(&r)) { t_skip("no window"); return; }
    CHECK(rl_put(&r, 1));
    PROCESS_INFORMATION pi;
    char args[16];
    snprintf(args, sizeof args, "%lu", GetCurrentProcessId());
    HANDLE ch = spawn_async("rl_replacer", args, &pi);
    if (!ch) { rl_end(&r); t_skip("cannot spawn child"); return; }
    EQ(wait_pump(ch, 20000), 0);
    CloseHandle(pi.hThread); CloseHandle(ch);
    CHECK(cd_open(r.hwnd));                                 /* the stale window can still open the clipboard ... */
    CHECK(U(GetClipboardOwner)() != r.hwnd);                /* ... but it is not the owner */
    CHECK(U(CloseClipboard)());
    rl_end(&r);
}

/* ---- child: owner process destroys its window and exits ---------------------------------------- */
CHILD(rl_owner_exit) {
    struct rl r;
    (void)argc;
    DWORD ppid = strtoul(argv[1], NULL, 10);
    memset(&r, 0, sizeof r);
    r.nfmt = 2; r.protocol = 1;
    r.fmt[0] = xfmt(ppid, "exit_imm"); r.fmt[1] = xfmt(ppid, "exit_dly");
    r.data[0] = "CHILD-IMMEDIATE"; r.data[1] = "CHILD-DELAYED";
    CH(rl_new(&r));
    if (!r.hwnd) return cf;
    BOOL ok = FALSE;
    for (int i = 0; i < 40 && !ok; i++) { ok = U(OpenClipboard)(r.hwnd); if (!ok) Sleep(50); }
    CH(ok);
    CH(U(EmptyClipboard)());
    CH(U(SetClipboardData)(r.fmt[0], cd_gstr_a(r.data[0])) != NULL); r.rendered[0] = 1;
    U(SetClipboardData)(r.fmt[1], NULL);
    CH(U(CloseClipboard)());
    U(DestroyWindow)(r.hwnd);
    CH(r.n_all == 1);
    CH(r.all_owner_is_me);
    CH(r.all_set_ok == 1);
    return cf;
}

TEST(clipboard_dde_renderlife_owner_process_exit_leaves_rendered_data) {
    CD_SETUP();
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    char args[32];
    snprintf(args, sizeof args, "%lu", GetCurrentProcessId());
    struct k_child_result r = k_child_run("rl_owner_exit", args, 30000);
    EQ(r.exit_code, 0);
    UINT fi = xfmt(GetCurrentProcessId(), "exit_imm"), fd = xfmt(GetCurrentProcessId(), "exit_dly");
    CHECK(U(IsClipboardFormatAvailable)(fi));
    CHECK(U(IsClipboardFormatAvailable)(fd));
    EQ(U(GetClipboardOwner)(), NULL);
    CHECK(cd_open(NULL));
    char s[32] = {0};
    CHECK(get_str(fi, s, sizeof s)); CHECK(!strcmp(s, "CHILD-IMMEDIATE"));
    memset(s, 0, sizeof s);
    CHECK(get_str(fd, s, sizeof s)); CHECK(!strcmp(s, "CHILD-DELAYED"));
    CHECK(U(CloseClipboard)());
}
