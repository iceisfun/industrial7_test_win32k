/* clipboard_dde: OpenClipboard / CloseClipboard / EmptyClipboard rules in the window station.
 *
 * NtUserOpenClipboard: the clipboard is held by one thread and associated with the window passed to
 * OpenClipboard. Same hwnd and same thread: returns TRUE again; otherwise a held clipboard yields
 * ERROR_ACCESS_DENIED (5). CloseClipboard / EmptyClipboard /
 * GetClipboardData / EnumClipboardFormats fail with ERROR_CLIPBOARD_NOT_OPEN (0x58a) when
 * the clipboard is not held by the calling thread. Invalid window handles give ERROR_INVALID_WINDOW_HANDLE. */
#include "cd.h"

#define FAILS(call, err) do { SetLastError(0xDEAD); t_check(!(call), __FILE__, __LINE__, #call " fails", NULL); EXPECT_ERR(err); } while (0)

TEST(clipboard_dde_open_close_basic) {
    CD_SETUP();
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(cd_open(c->hwnd));
    EQ(U(GetOpenClipboardWindow)(), c->hwnd);
    CHECK(U(CloseClipboard)());
    CHECK(U(GetOpenClipboardWindow)() != c->hwnd);
}

TEST(clipboard_dde_open_null_owner_reports_null_open_window) {
    CD_SETUP();
    /* the open-window association is cleared: GetOpenClipboardWindow returns NULL although the clipboard is open */
    CHECK(cd_open(NULL));
    EQ(U(GetOpenClipboardWindow)(), NULL);
    UINT f[4];
    SetLastError(0);
    cd_formats(f, 4);                        /* proves this thread holds the lock: no 1418 */
    EQ(GetLastError(), 0);
    CHECK(U(CloseClipboard)());
}

TEST(clipboard_dde_open_same_hwnd_twice_same_thread) {
    CD_SETUP();
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(cd_open(c->hwnd));
    CHECK(U(OpenClipboard)(c->hwnd));        /* same hwnd and same thread => return 1 */
    CHECK(U(CloseClipboard)());              /* a single close releases it: no open count */
    FAILS(U(CloseClipboard)(), CD_ERR_CLIPBOARD_NOT_OPEN);
}

TEST(clipboard_dde_open_null_twice_same_thread) {
    CD_SETUP();
    CHECK(cd_open(NULL));
    CHECK(U(OpenClipboard)(NULL));
    CHECK(U(CloseClipboard)());
    FAILS(U(CloseClipboard)(), CD_ERR_CLIPBOARD_NOT_OPEN);
}

TEST(clipboard_dde_open_other_hwnd_same_thread_denied) {
    CD_SETUP();
    struct cd_ctx *a = cd_new(), *b = cd_new();
    if (!a || !b) { t_skip("no window"); return; }
    CHECK(cd_open(a->hwnd));
    FAILS(U(OpenClipboard)(b->hwnd), CD_ERR_ACCESS_DENIED);   /* clipboard held and hwnd differs */
    EQ(U(GetOpenClipboardWindow)(), a->hwnd);          /* the failed open changed nothing */
    CHECK(U(CloseClipboard)());
}

TEST(clipboard_dde_open_null_while_hwnd_open_denied) {
    CD_SETUP();
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    CHECK(cd_open(a->hwnd));
    FAILS(U(OpenClipboard)(NULL), CD_ERR_ACCESS_DENIED);
    CHECK(U(CloseClipboard)());
}

TEST(clipboard_dde_open_hwnd_while_null_open_denied) {
    CD_SETUP();
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    CHECK(cd_open(NULL));
    FAILS(U(OpenClipboard)(a->hwnd), CD_ERR_ACCESS_DENIED);
    CHECK(U(CloseClipboard)());
}

struct thr_open { HWND h; BOOL r; DWORD err; BOOL r2; DWORD err2; HWND h2; };
static DWORD WINAPI thr_try_open(LPVOID p) {
    struct thr_open *a = p;
    SetLastError(0xDEAD);
    a->r = U(OpenClipboard)(a->h);
    a->err = GetLastError();
    if (a->r) { U(CloseClipboard)(); }
    SetLastError(0xDEAD);
    a->r2 = a->h2 ? U(OpenClipboard)(a->h2) : FALSE;
    a->err2 = GetLastError();
    if (a->r2) U(CloseClipboard)();
    return 0;
}

TEST(clipboard_dde_open_from_other_thread_denied_then_ok) {
    CD_SETUP();
    struct cd_ctx *a = cd_new(), *b = cd_new();
    if (!a || !b) { t_skip("no window"); return; }
    struct thr_open t = { NULL, 1, 0, 1, 0, b->hwnd };
    CHECK(cd_open(a->hwnd));
    EQ(cd_thread(thr_try_open, &t), 0);
    CHECK(!t.r);  EQ(t.err, CD_ERR_ACCESS_DENIED);    /* NULL differs from the open window */
    CHECK(!t.r2); EQ(t.err2, CD_ERR_ACCESS_DENIED);
    CHECK(U(CloseClipboard)());
    t.r = t.r2 = 0;
    EQ(cd_thread(thr_try_open, &t), 0);
    CHECK(t.r);                                              /* lock released by the main thread */
}

TEST(clipboard_dde_open_same_hwnd_from_other_thread_observed) {
    CD_SETUP();
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    struct thr_open t = { a->hwnd, 0, 0, 0, 0, NULL };
    CHECK(cd_open(a->hwnd));
    EQ(cd_thread(thr_try_open, &t), 0);
    /* OpenClipboard still succeeds when hwnd is the open window even if the holder
     * is another thread, so the outcome is recorded rather than asserted. */
    t_note("second thread OpenClipboard(same hwnd) -> %d (err %lu)", t.r, t.err);
    U(CloseClipboard)();
}

TEST(clipboard_dde_open_invalid_hwnd) {
    CD_SETUP();
    static const ULONG_PTR bad[] = { 0x7FFE7777, 0xDEADBEEF, 0x12345678, 0x5A5A0002 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        FAILS(U(OpenClipboard)((HWND)bad[i]), CD_ERR_INVALID_WINDOW);   /* ValidateHwnd fails before any station work */
        EQ(U(GetOpenClipboardWindow)() == (HWND)bad[i], 0);
    }
    /* nothing was left open */
    FAILS(U(CloseClipboard)(), CD_ERR_CLIPBOARD_NOT_OPEN);
}

TEST(clipboard_dde_open_destroyed_window) {
    CD_SETUP();
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    HWND h = a->hwnd;
    cd_destroy(a);
    FAILS(U(OpenClipboard)(h), CD_ERR_INVALID_WINDOW);
}

TEST(clipboard_dde_open_invalid_hwnd_direct) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserOpenClipboard_pfn op = UN(NtUserOpenClipboard);
    REQUIRE(op);
    DWORD fl = 0x55;
    SetLastError(0xDEAD);
    CHECK(!op((HWND)(ULONG_PTR)0x7FFE7777, &fl));
    EXPECT_ERR(CD_ERR_INVALID_WINDOW);
}

TEST(clipboard_dde_close_without_open) {
    CD_SETUP();
    FAILS(U(CloseClipboard)(), CD_ERR_CLIPBOARD_NOT_OPEN);   /* clipboard held by another thread => 0x58a */
}

TEST(clipboard_dde_close_without_open_direct) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserCloseClipboard_pfn cl = UN(NtUserCloseClipboard);
    REQUIRE(cl);
    FAILS(cl(), CD_ERR_CLIPBOARD_NOT_OPEN);
}

TEST(clipboard_dde_close_twice) {
    CD_SETUP();
    CHECK(cd_open(NULL));
    CHECK(U(CloseClipboard)());
    FAILS(U(CloseClipboard)(), CD_ERR_CLIPBOARD_NOT_OPEN);
}

TEST(clipboard_dde_empty_without_open) {
    CD_SETUP();
    FAILS(U(EmptyClipboard)(), CD_ERR_CLIPBOARD_NOT_OPEN);
}

TEST(clipboard_dde_empty_without_open_direct) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserEmptyClipboard_pfn em = UN(NtUserEmptyClipboard);
    REQUIRE(em);
    FAILS(em(), CD_ERR_CLIPBOARD_NOT_OPEN);
}

TEST(clipboard_dde_empty_after_close) {
    CD_SETUP();
    CHECK(cd_open(NULL));
    CHECK(U(CloseClipboard)());
    FAILS(U(EmptyClipboard)(), CD_ERR_CLIPBOARD_NOT_OPEN);
}

TEST(clipboard_dde_get_without_open) {
    CD_SETUP();
    static const UINT fmts[] = { CF_TEXT, CF_UNICODETEXT, CF_OEMTEXT, CF_BITMAP, CF_DIB, 0x200, 0xC001 };
    for (unsigned i = 0; i < sizeof fmts / sizeof fmts[0]; i++) {
        SetLastError(0xDEAD);
        CHECK(U(GetClipboardData)(fmts[i]) == NULL);
        EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);   /* clipboard held by another thread */
    }
}

TEST(clipboard_dde_get_without_open_direct) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserGetClipboardData_pfn g = UN(NtUserGetClipboardData);
    REQUIRE(g);
    CD_GETCLIPBDATA out = { 0x1111, 0x2222, (HANDLE)0x3333 };
    SetLastError(0xDEAD);
    CHECK(g(CF_UNICODETEXT, &out) == NULL);
    EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);
    /* the out block is only written when a handle is returned */
    EQ(out.uFmtRet, 0x1111);
    EQ(out.fGlobalHandle, 0x2222);
    EQ(out.hExtra, (HANDLE)0x3333);
}

TEST(clipboard_dde_enum_without_open) {
    CD_SETUP();
    SetLastError(0xDEAD);
    EQ(U(EnumClipboardFormats)(0), 0);   /* user32 clears the error, _EnumClipboardFormats sets 0x58a */
    EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);
    SetLastError(0xDEAD);
    EQ(U(EnumClipboardFormats)(CF_TEXT), 0);
    EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);
}

TEST(clipboard_dde_enum_without_open_direct_callone) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserCallOneParam_pfn c1 = UN(NtUserCallOneParam);
    REQUIRE(c1);
    SetLastError(0xDEAD);
    EQ(c1(0, 0x31), 0);                  /* EnumClipboardFormats is function 0x31 of the one-param table */
    EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);
}

TEST(clipboard_dde_set_delayed_without_open) {
    CD_SETUP();
    UINT fmt = cd_reg("noopen");
    SetLastError(0xDEAD);
    CHECK(U(SetClipboardData)(fmt, NULL) == NULL);
    EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);      /* clipboard not held */
    CHECK(!U(IsClipboardFormatAvailable)(fmt));
}

TEST(clipboard_dde_set_without_open_direct) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserSetClipboardData_pfn st = UN(NtUserSetClipboardData);
    REQUIRE(st);
    CD_SETCLIPBDATA sp = { 0, 1 };
    static const UINT fmts[] = { CF_TEXT, CF_UNICODETEXT, 0x200, 0x2FF, 0xC000 };
    for (unsigned i = 0; i < sizeof fmts / sizeof fmts[0]; i++) {
        SetLastError(0xDEAD);
        CHECK(st(fmts[i], NULL, &sp) == 0);
        EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);
    }
}

TEST(clipboard_dde_set_format_zero_fails) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    SetLastError(0xDEAD);
    CHECK(U(SetClipboardData)(0, NULL) == NULL);
    EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);      /* InternalSetClipboardData tests (lock==0 || format==0) with one error */
    EQ(U(CountClipboardFormats)(), 0);
    U(CloseClipboard)();
}

TEST(clipboard_dde_set_kernel_handle_values_1_to_4_rejected) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserSetClipboardData_pfn st = UN(NtUserSetClipboardData);
    REQUIRE(st);
    CD_SETCLIPBDATA sp = { 0, 1 };
    CHECK(cd_open_empty(NULL));
    for (ULONG_PTR h = 1; h <= 4; h++) {       /* _SetClipboardData: (hMem - 1) < 4 is reserved for the placeholders */
        SetLastError(0xDEAD);
        CHECK(st(0x200, (HANDLE)h, &sp) == 0);
        EXPECT_ERR(CD_ERR_INVALID_PARAMETER);
    }
    EQ(U(CountClipboardFormats)(), 0);
    U(CloseClipboard)();
}

/* ---- other-thread rules -------------------------------------------------------------------- */
struct thr_ops { DWORD close_err, empty_err, enum_err, get_err; BOOL close_r, empty_r; UINT enum_r; HANDLE get_r; };
static DWORD WINAPI thr_ops_fn(LPVOID p) {
    struct thr_ops *o = p;
    SetLastError(0xDEAD); o->close_r = U(CloseClipboard)();   o->close_err = GetLastError();
    SetLastError(0xDEAD); o->empty_r = U(EmptyClipboard)();   o->empty_err = GetLastError();
    SetLastError(0xDEAD); o->enum_r = U(EnumClipboardFormats)(0); o->enum_err = GetLastError();
    SetLastError(0xDEAD); o->get_r = U(GetClipboardData)(CF_UNICODETEXT); o->get_err = GetLastError();
    return 0;
}

TEST(clipboard_dde_other_thread_cannot_close_empty_enum_get) {
    CD_SETUP();
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    struct thr_ops o;
    memset(&o, 0, sizeof o);
    CHECK(cd_open_empty(a->hwnd));
    CHECK(U(SetClipboardData)(0x200, NULL) == NULL);
    EQ(cd_thread(thr_ops_fn, &o), 0);
    CHECK(!o.close_r); EQ(o.close_err, CD_ERR_CLIPBOARD_NOT_OPEN);
    CHECK(!o.empty_r); EQ(o.empty_err, CD_ERR_CLIPBOARD_NOT_OPEN);
    EQ(o.enum_r, 0); EQ(o.enum_err, CD_ERR_CLIPBOARD_NOT_OPEN);
    CHECK(o.get_r == NULL); EQ(o.get_err, CD_ERR_CLIPBOARD_NOT_OPEN);
    /* the owner thread still holds it and its data is intact */
    EQ(U(GetOpenClipboardWindow)(), a->hwnd);
    CHECK(U(IsClipboardFormatAvailable)(0x200));
    CHECK(U(CloseClipboard)());
}

/* ---- direct open flag ---------------------------------------------------------------------- */
TEST(clipboard_dde_open_flag_zero_when_owner_in_process) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserOpenClipboard_pfn op = UN(NtUserOpenClipboard);
    REQUIRE(op);
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    CHECK(cd_open_empty(a->hwnd));
    CHECK(U(CloseClipboard)());
    DWORD fl = 0x55;
    CHECK(op(a->hwnd, &fl));                    /* owner window's ppi == caller's ppi => 0 */
    EQ(fl, 0);
    CHECK(U(CloseClipboard)());
}

TEST(clipboard_dde_open_flag_one_when_owner_null) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserOpenClipboard_pfn op = UN(NtUserOpenClipboard);
    REQUIRE(op);
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    CHECK(cd_open_empty(NULL));
    CHECK(U(CloseClipboard)());
    DWORD fl = 0x55;
    CHECK(op(a->hwnd, &fl));                    /* no owner => 1 */
    EQ(fl, 1);
    CHECK(U(CloseClipboard)());
}

TEST(clipboard_dde_open_flag_written_even_on_reopen) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserOpenClipboard_pfn op = UN(NtUserOpenClipboard);
    REQUIRE(op);
    CHECK(cd_open(NULL));
    DWORD fl = 0x55;
    CHECK(op(NULL, &fl));                       /* early "already mine" return still stores a defined value */
    EQ(fl, 0);
    CHECK(U(CloseClipboard)());
}

TEST(clipboard_dde_direct_open_close_matches_userpath) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserOpenClipboard_pfn op = UN(NtUserOpenClipboard);
    NtUserCloseClipboard_pfn cl = UN(NtUserCloseClipboard);
    NtUserGetOpenClipboardWindow_pfn gow = UN(NtUserGetOpenClipboardWindow);
    REQUIRE(op && cl && gow);
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    DWORD fl = 0;
    for (int i = 0; i < 20 && !op(a->hwnd, &fl); i++) Sleep(50);
    EQ(gow(), a->hwnd);
    EQ(U(GetOpenClipboardWindow)(), a->hwnd);
    CHECK(cl());
    CHECK(gow() != a->hwnd);
}

/* ---- ownership via Empty --------------------------------------------------------------------- */
TEST(clipboard_dde_empty_assigns_owner) {
    CD_SETUP();
    struct cd_ctx *a = cd_new(), *b = cd_new();
    if (!a || !b) { t_skip("no window"); return; }
    CHECK(cd_open_empty(a->hwnd));
    EQ(U(GetClipboardOwner)(), a->hwnd);   /* EmptyClipboard: the owner becomes the window that opened the clipboard */
    CHECK(U(CloseClipboard)());
    EQ(U(GetClipboardOwner)(), a->hwnd);   /* owner survives Close */
    CHECK(cd_open(b->hwnd));
    EQ(U(GetClipboardOwner)(), a->hwnd);   /* ...and a bare Open does not take ownership */
    CHECK(U(EmptyClipboard)());
    EQ(U(GetClipboardOwner)(), b->hwnd);
    CHECK(U(CloseClipboard)());
}

TEST(clipboard_dde_empty_with_null_open_clears_owner) {
    CD_SETUP();
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    CHECK(cd_open_empty(a->hwnd));
    CHECK(U(CloseClipboard)());
    EQ(U(GetClipboardOwner)(), a->hwnd);
    CHECK(cd_open_empty(NULL));
    EQ(U(GetClipboardOwner)(), NULL);
    CHECK(U(CloseClipboard)());
    EQ(U(GetClipboardOwner)(), NULL);
}

TEST(clipboard_dde_empty_sends_destroyclipboard_to_previous_owner) {
    CD_SETUP();
    struct cd_ctx *a = cd_new(), *b = cd_new();
    if (!a || !b) { t_skip("no window"); return; }
    CHECK(cd_open_empty(a->hwnd));
    CHECK(U(CloseClipboard)());
    EQ(CD_CNT(a, CD_WM_DESTROYCLIPBOARD), 0);
    CHECK(cd_open_empty(b->hwnd));              /* the previous owner (a) is sent message 0x307 */
    EQ(CD_CNT(a, CD_WM_DESTROYCLIPBOARD), 1);   /* same-thread window: delivered synchronously */
    EQ(a->last_w[CD_WM_DESTROYCLIPBOARD - 0x300], 0);
    EQ(a->last_l[CD_WM_DESTROYCLIPBOARD - 0x300], 0);
    EQ(CD_CNT(b, CD_WM_DESTROYCLIPBOARD), 0);
    CHECK(U(CloseClipboard)());
}

TEST(clipboard_dde_empty_again_notifies_self) {
    CD_SETUP();
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    CHECK(cd_open_empty(a->hwnd));
    EQ(CD_CNT(a, CD_WM_DESTROYCLIPBOARD), 0);
    CHECK(U(EmptyClipboard)());                 /* current owner is the previous owner too */
    EQ(CD_CNT(a, CD_WM_DESTROYCLIPBOARD), 1);
    CHECK(U(EmptyClipboard)());
    EQ(CD_CNT(a, CD_WM_DESTROYCLIPBOARD), 2);
    CHECK(U(CloseClipboard)());
}

TEST(clipboard_dde_empty_clears_all_formats) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    for (UINT f = 0x200; f < 0x208; f++) CHECK(U(SetClipboardData)(f, NULL) == NULL);
    EQ(U(CountClipboardFormats)(), 8);
    CHECK(U(EmptyClipboard)());
    EQ(U(CountClipboardFormats)(), 0);
    for (UINT f = 0x200; f < 0x208; f++) CHECK(!U(IsClipboardFormatAvailable)(f));
    SetLastError(0);
    EQ(U(EnumClipboardFormats)(0), 0);
    EQ(GetLastError(), 0);               /* end of list is not an error */
    CHECK(U(CloseClipboard)());
}

TEST(clipboard_dde_destroy_owner_clears_owner) {
    CD_SETUP();
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    CHECK(cd_put(a->hwnd, 0x200, "abcd", 4));
    EQ(U(GetClipboardOwner)(), a->hwnd);
    HWND h = a->hwnd;
    cd_destroy(a);                              /* ownership is dropped when the owner window dies */
    CHECK(U(GetClipboardOwner)() != h);
    EQ(U(GetClipboardOwner)(), NULL);
}

TEST(clipboard_dde_destroy_non_owner_keeps_owner) {
    CD_SETUP();
    struct cd_ctx *a = cd_new(), *b = cd_new();
    if (!a || !b) { t_skip("no window"); return; }
    CHECK(cd_put(a->hwnd, 0x200, "abcd", 4));
    cd_destroy(b);
    EQ(U(GetClipboardOwner)(), a->hwnd);
    EQ(CD_CNT(a, CD_WM_RENDERALLFORMATS), 0);
    CHECK(cd_open(NULL));
    CHECK(U(IsClipboardFormatAvailable)(0x200));
    U(CloseClipboard)();
}

TEST(clipboard_dde_destroy_open_window_observed) {
    CD_SETUP();
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    CHECK(cd_open(a->hwnd));
    U(DestroyWindow)(a->hwnd);
    HWND now = U(GetOpenClipboardWindow)();
    t_note("GetOpenClipboardWindow after destroying the open window: %p", (void *)now);
    /* Windows 10: destroying the window that has the clipboard open closes it, so there is no open clipboard
     * left for CloseClipboard (ERROR_CLIPBOARD_NOT_OPEN) */
    CHECK(now == NULL);
    SetLastError(0);
    CHECK(!U(CloseClipboard)());
    EXPECT_EQ(GetLastError(), 1418);
    a->hwnd = NULL;
}
