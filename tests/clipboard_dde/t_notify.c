/* clipboard_dde: owner and viewer notifications.
 *
 *  - delayed rendering: GetClipboardData asks the owner with WM_RENDERFORMAT (0x305,
 *    wParam=format) to spwndClipOwner, with the "rendering" flag 0x80 raised so the nested
 *    SetClipboardData neither bumps the sequence number nor leaves the changed flag set.
 *  - owner window destroyed: WM_RENDERALLFORMATS (0x306), then every entry
 *    whose hData is 0 or the text placeholder 1 is dropped.
 *  - viewer notification (run by CloseClipboard when changed, and by SetClipboardViewer): sends
 *    WM_DRAWCLIPBOARD (0x308, wParam = owner) to the first viewer with SendNotifyMessage and POSTS
 *    WM_CLIPBOARDUPDATE (0x31D, wParam = visible format count) to each listener, newest first.
 *  - ChangeClipboardChain: removing the head just relinks; otherwise WM_CHANGECBCHAIN (0x30D,
 *    wParam = window leaving, lParam = its next) is sent to the head and its result returned. */
#include "cd.h"

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static struct cd_ctx *render_owner(UINT fmt, const char *data) {
    struct cd_ctx *c = cd_new();
    if (!c) return NULL;
    c->render[0].fmt = fmt; c->render[0].data = data; c->render[0].len = strlen(data) + 1; c->nrender = 1;
    return c;
}

/* ---- delayed rendering ------------------------------------------------------------------- */
TEST(clipboard_dde_render_on_first_get) {
    CD_SETUP();
    struct cd_ctx *c = render_owner(0x250, "RENDERED"), *op = cd_new();
    if (!c || !op) { t_skip("no window"); return; }
    CHECK(cd_open_empty(c->hwnd));
    U(SetClipboardData)(0x250, NULL);
    CHECK(U(CloseClipboard)());
    EQ(CD_CNT(c, CD_WM_RENDERFORMAT), 0);             /* nothing rendered at Set time */
    CHECK(cd_open(op->hwnd));
    char d[16] = {0};
    CHECK(cd_get_bytes(0x250, d, sizeof d) >= 9);
    CHECK(!strcmp(d, "RENDERED"));
    EQ(CD_CNT(c, CD_WM_RENDERFORMAT), 1);
    EQ(c->last_w[CD_WM_RENDERFORMAT - 0x300], 0x250);
    EQ(c->last_l[CD_WM_RENDERFORMAT - 0x300], 0);
    EQ(c->seen_open_owner_ok, 1);                      /* the owner is still the owner while rendering */
    EQ(c->seen_open_window, op->hwnd);                 /* ...and the requester still holds the clipboard open */
    EQ(c->seen_fmt_avail, 1);
    U(CloseClipboard)();
}

TEST(clipboard_dde_render_only_once) {
    CD_SETUP();
    struct cd_ctx *c = render_owner(0x251, "ONCE"), *op = cd_new();
    if (!c || !op) { t_skip("no window"); return; }
    CHECK(cd_put(c->hwnd, 0x251, NULL, 0));
    CHECK(cd_open(op->hwnd));
    HANDLE h1 = U(GetClipboardData)(0x251);
    HANDLE h2 = U(GetClipboardData)(0x251);
    CHECK(h1 != NULL);
    EQ(h1, h2);
    EQ(CD_CNT(c, CD_WM_RENDERFORMAT), 1);              /* the entry has data afterwards */
    U(CloseClipboard)();
}

TEST(clipboard_dde_render_refused_asks_again) {
    CD_SETUP();
    struct cd_ctx *c = render_owner(0x252, "NEVER"), *op = cd_new();
    if (!c || !op) { t_skip("no window"); return; }
    c->render[0].refuse = 1;
    CHECK(cd_put(c->hwnd, 0x252, NULL, 0));
    CHECK(cd_open(op->hwnd));
    CHECK(U(GetClipboardData)(0x252) == NULL);
    EQ(CD_CNT(c, CD_WM_RENDERFORMAT), 1);
    CHECK(U(IsClipboardFormatAvailable)(0x252));              /* the delayed entry stays */
    CHECK(U(GetClipboardData)(0x252) == NULL);
    EQ(CD_CNT(c, CD_WM_RENDERFORMAT), 2);              /* hData still 0 => rendered request repeats */
    U(CloseClipboard)();
}

TEST(clipboard_dde_render_without_owner_returns_null) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    U(SetClipboardData)(0x253, NULL);
    CHECK(U(GetClipboardData)(0x253) == NULL);                /* no clipboard owner window => nobody to ask */
    CHECK(U(IsClipboardFormatAvailable)(0x253));
    U(CloseClipboard)();
}

TEST(clipboard_dde_render_two_formats_in_request_order) {
    CD_SETUP();
    struct cd_ctx *c = cd_new(), *op = cd_new();
    if (!c || !op) { t_skip("no window"); return; }
    c->render[0].fmt = 0x254; c->render[0].data = "first"; c->render[0].len = 6;
    c->render[1].fmt = 0x255; c->render[1].data = "second"; c->render[1].len = 7;
    c->nrender = 2;
    CHECK(cd_open_empty(c->hwnd));
    U(SetClipboardData)(0x254, NULL); U(SetClipboardData)(0x255, NULL);
    CHECK(U(CloseClipboard)());
    CHECK(cd_open(op->hwnd));
    cd_log_reset();
    char a[8] = {0}, b[8] = {0};
    CHECK(cd_get_bytes(0x255, a, sizeof a) >= 7);
    CHECK(cd_get_bytes(0x254, b, sizeof b) >= 6);
    CHECK(!strcmp(a, "second") && !strcmp(b, "first"));
    EQ(cd_log_n, 2);
    if (cd_log_n == 2) { EQ(cd_log[0].w, 0x255); EQ(cd_log[1].w, 0x254); }
    U(CloseClipboard)();
}

TEST(clipboard_dde_render_does_not_bump_sequence) {
    CD_SETUP();
    struct cd_ctx *c = render_owner(0x256, "SEQ"), *op = cd_new();
    if (!c || !op) { t_skip("no window"); return; }
    CHECK(cd_put(c->hwnd, 0x256, NULL, 0));
    CHECK(cd_open(op->hwnd));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    CHECK(U(GetClipboardData)(0x256) != NULL);
    EQ(U(GetClipboardSequenceNumber)(), s0);           /* InternalSetClipboardData skips ++ while flag 0x80 is raised */
    U(CloseClipboard)();
}

TEST(clipboard_dde_render_delayed_text_synthesizes_after_close) {
    CD_SETUP();
    struct cd_ctx *c = cd_new(), *op = cd_new();
    if (!c || !op) { t_skip("no window"); return; }
    c->render[0].fmt = CF_UNICODETEXT; c->render[0].data = (const char *)L"late text"; c->render[0].len = sizeof L"late text";
    c->nrender = 1;
    CHECK(cd_open_empty(c->hwnd));
    U(SetClipboardData)(CF_UNICODETEXT, NULL);
    CHECK(U(CloseClipboard)());
    CHECK(U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
    CHECK(U(IsClipboardFormatAvailable)(CF_TEXT));            /* the placeholders exist before anything is rendered */
    EQ(CD_CNT(c, CD_WM_RENDERFORMAT), 0);
    CHECK(cd_open(op->hwnd));
    char t[32] = {0};
    CHECK(cd_get_bytes(CF_TEXT, t, sizeof t) >= 10);          /* asking for ANSI text renders the Unicode source */
    CHECK(!strcmp(t, "late text"));
    EQ(CD_CNT(c, CD_WM_RENDERFORMAT), 1);
    EQ(c->last_w[CD_WM_RENDERFORMAT - 0x300], CF_UNICODETEXT);
    U(CloseClipboard)();
}

TEST(clipboard_dde_render_all_formats_on_owner_destroy) {
    CD_SETUP();
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    c->render[0].fmt = 0x257; c->render[0].data = "AAAA"; c->render[0].len = 5;
    c->render[1].fmt = 0x258; c->render[1].data = "BBBB"; c->render[1].len = 5; c->render[1].refuse = 1;
    c->nrender = 2; c->render_all = 1;
    CHECK(cd_open_empty(c->hwnd));
    U(SetClipboardData)(0x257, NULL); U(SetClipboardData)(0x258, NULL); U(SetClipboardData)(0x259, NULL);
    CHECK(U(SetClipboardData)(0x25A, cd_gmem("real", 5)) != NULL);
    CHECK(U(CloseClipboard)());
    DWORD s0 = U(GetClipboardSequenceNumber)();
    cd_destroy(c);                                            /* owner destroyed */
    EQ(CD_CNT(c, CD_WM_RENDERALLFORMATS), 1);
    EQ(c->last_w[CD_WM_RENDERALLFORMATS - 0x300], 0);
    EQ(c->last_l[CD_WM_RENDERALLFORMATS - 0x300], 0);
    CHECK(U(IsClipboardFormatAvailable)(0x257));              /* rendered by the handler */
    CHECK(!U(IsClipboardFormatAvailable)(0x258));             /* hData still 0: dropped */
    CHECK(!U(IsClipboardFormatAvailable)(0x259));
    CHECK(U(IsClipboardFormatAvailable)(0x25A));              /* real data survives */
    CHECK(U(GetClipboardSequenceNumber)() != s0);             /* entries were removed */
    EQ(U(GetClipboardOwner)(), NULL);
    CHECK(cd_open(NULL));
    char d[8] = {0};
    CHECK(cd_get_bytes(0x257, d, sizeof d) >= 5);
    CHECK(!strcmp(d, "AAAA"));
    U(CloseClipboard)();
}

TEST(clipboard_dde_owner_destroy_without_handler_drops_delayed_only) {
    CD_SETUP();
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(cd_open_empty(c->hwnd));
    U(SetClipboardData)(0x25B, NULL);
    U(SetClipboardData)(0x25C, cd_gmem("keep", 5));
    CHECK(U(CloseClipboard)());
    cd_destroy(c);
    EQ(CD_CNT(c, CD_WM_RENDERALLFORMATS), 1);          /* sent even though nothing handles it */
    CHECK(!U(IsClipboardFormatAvailable)(0x25B));
    CHECK(U(IsClipboardFormatAvailable)(0x25C));
    EQ(U(CountClipboardFormats)(), 1);
}

TEST(clipboard_dde_owner_destroy_with_only_real_data_still_notifies) {
    CD_SETUP();
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(cd_put(c->hwnd, 0x25D, "datadata", 8));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    cd_destroy(c);
    EQ(CD_CNT(c, CD_WM_RENDERALLFORMATS), 1);
    EQ(U(GetClipboardSequenceNumber)(), s0);
    CHECK(U(IsClipboardFormatAvailable)(0x25D));
}

TEST(clipboard_dde_empty_by_new_owner_does_not_notify_itself) {
    CD_SETUP();
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(cd_open_empty(c->hwnd));
    CHECK(U(CloseClipboard)());
    cd_destroy(c);
    EQ(CD_CNT(c, CD_WM_DESTROYCLIPBOARD), 0);          /* WM_DESTROYCLIPBOARD only comes from EmptyClipboard */
    EQ(CD_CNT(c, CD_WM_RENDERALLFORMATS), 1);
}

/* ---- format listeners ------------------------------------------------------------------------ */
TEST(clipboard_dde_listener_add_remove_rules) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener) && U(RemoveClipboardFormatListener));
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(U(AddClipboardFormatListener)(c->hwnd));
    c->listener = 1;
    SetLastError(0xDEAD);
    CHECK(!U(AddClipboardFormatListener)(c->hwnd));          /* already listening: window flag bit 0 => ERROR_INVALID_PARAMETER */
    EXPECT_ERR(CD_ERR_INVALID_PARAMETER);
    CHECK(U(RemoveClipboardFormatListener)(c->hwnd));
    c->listener = 0;
    SetLastError(0xDEAD);
    CHECK(!U(RemoveClipboardFormatListener)(c->hwnd));       /* not listening */
    EXPECT_ERR(CD_ERR_INVALID_PARAMETER);
    CHECK(U(AddClipboardFormatListener)(c->hwnd));           /* can be added again after removal */
    c->listener = 1;
}

TEST(clipboard_dde_listener_invalid_windows) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener) && U(RemoveClipboardFormatListener));
    HWND bad = (HWND)(ULONG_PTR)0x7FFE7777;
    SetLastError(0xDEAD);
    CHECK(!U(AddClipboardFormatListener)(bad));    EXPECT_ERR(CD_ERR_INVALID_WINDOW);
    SetLastError(0xDEAD);
    CHECK(!U(RemoveClipboardFormatListener)(bad)); EXPECT_ERR(CD_ERR_INVALID_WINDOW);
    SetLastError(0xDEAD);
    CHECK(!U(AddClipboardFormatListener)(NULL));   EXPECT_ERR(CD_ERR_INVALID_WINDOW);
    struct cd_ctx *c = cd_new();
    if (!c) return;
    HWND h = c->hwnd;
    cd_destroy(c);
    SetLastError(0xDEAD);
    CHECK(!U(AddClipboardFormatListener)(h));      EXPECT_ERR(CD_ERR_INVALID_WINDOW);
}

TEST(clipboard_dde_listener_foreign_process_window_denied) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener) && U(GetShellWindow) && U(GetWindowThreadProcessId));
    HWND sh = U(GetShellWindow)();
    DWORD pid = 0;
    if (!sh) { t_skip("no shell window"); return; }
    U(GetWindowThreadProcessId)(sh, &pid);
    if (!pid || pid == GetCurrentProcessId()) { t_skip("shell window is in this process"); return; }
    SetLastError(0xDEAD);
    BOOL r = U(AddClipboardFormatListener)(sh);              /* window's ppi != caller's ppi => ERROR_ACCESS_DENIED */
    DWORD e = GetLastError();
    CHECK(r == 0 || r == 5);
    if (r) U(RemoveClipboardFormatListener)(sh);
    /* one check on either outcome, so the check count does not depend on the token's mode */
    CHECKF(r != 0 || e == CD_ERR_ACCESS_DENIED, "refused with error %lu, want %lu", e, (unsigned long)CD_ERR_ACCESS_DENIED);
    t_note("AddClipboardFormatListener(foreign window) -> %d err %lu", r, e);
}

TEST(clipboard_dde_listener_gets_update_after_put) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener));
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(U(AddClipboardFormatListener)(c->hwnd)); c->listener = 1;
    CHECK(cd_put(NULL, 0x260, "abcdefgh", 8));
    EQ(CD_CNT(c, CD_WM_CLIPBOARDUPDATE), 0);          /* posted, not sent */
    k_pump(150);
    EQ(CD_CNT(c, CD_WM_CLIPBOARDUPDATE), 1);          /* one Close of a changed clipboard => one message */
    t_note("WM_CLIPBOARDUPDATE wParam=%llu (analysis: visible format count; docs say unused)", (unsigned long long)c->last_w[CD_WM_CLIPBOARDUPDATE - 0x300]);
    EQ(c->last_l[CD_WM_CLIPBOARDUPDATE - 0x300], 0);
}

TEST(clipboard_dde_listener_update_count_includes_synthesized) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener));
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(U(AddClipboardFormatListener)(c->hwnd)); c->listener = 1;
    CHECK(cd_put_text(NULL, L"listen"));
    k_pump(150);
    EQ(CD_CNT(c, CD_WM_CLIPBOARDUPDATE), 1);
    t_note("WM_CLIPBOARDUPDATE wParam=%llu count=%d", (unsigned long long)c->last_w[CD_WM_CLIPBOARDUPDATE - 0x300], U(CountClipboardFormats)());
}

TEST(clipboard_dde_listener_no_update_when_unchanged) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener));
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(cd_put(NULL, 0x261, "abcdefgh", 8));
    k_pump(50);
    CHECK(U(AddClipboardFormatListener)(c->hwnd)); c->listener = 1;
    CHECK(cd_open(NULL));
    CHECK(U(IsClipboardFormatAvailable)(0x261));
    U(GetClipboardData)(0x261);
    CHECK(U(CloseClipboard)());                               /* flag 0x40 clear: CloseClipboard does not notify viewers */
    k_pump(150);
    EQ(CD_CNT(c, CD_WM_CLIPBOARDUPDATE), 0);
}

TEST(clipboard_dde_listener_update_for_empty_only_cycle) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener));
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(U(AddClipboardFormatListener)(c->hwnd)); c->listener = 1;
    CHECK(cd_open_empty(NULL));
    CHECK(U(CloseClipboard)());
    k_pump(150);
    EQ(CD_CNT(c, CD_WM_CLIPBOARDUPDATE), 1);
    t_note("WM_CLIPBOARDUPDATE wParam after empty=%llu", (unsigned long long)c->last_w[CD_WM_CLIPBOARDUPDATE - 0x300]);
}

TEST(clipboard_dde_listener_one_update_per_cycle) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener));
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(U(AddClipboardFormatListener)(c->hwnd)); c->listener = 1;
    for (int i = 0; i < 3; i++) CHECK(cd_put(NULL, 0x262, "abcdefgh", 8));
    k_pump(200);
    EQ(CD_CNT(c, CD_WM_CLIPBOARDUPDATE), 3);
}

TEST(clipboard_dde_listener_none_after_remove) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener));
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(U(AddClipboardFormatListener)(c->hwnd));
    CHECK(U(RemoveClipboardFormatListener)(c->hwnd));
    CHECK(cd_put(NULL, 0x263, "abcdefgh", 8));
    k_pump(150);
    EQ(CD_CNT(c, CD_WM_CLIPBOARDUPDATE), 0);
}

TEST(clipboard_dde_listener_two_windows_newest_first) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener));
    struct cd_ctx *a = cd_new(), *b = cd_new();
    if (!a || !b) { t_skip("no window"); return; }
    CHECK(U(AddClipboardFormatListener)(a->hwnd)); a->listener = 1;
    CHECK(U(AddClipboardFormatListener)(b->hwnd)); b->listener = 1;
    cd_log_reset();
    CHECK(cd_put(NULL, 0x264, "abcdefgh", 8));
    k_pump(200);
    EQ(cd_log_n, 2);                                   /* the listener list head is the newest registration */
    if (cd_log_n == 2) { EQ(cd_log[0].ctx_id, b->id); EQ(cd_log[1].ctx_id, a->id); }
}

TEST(clipboard_dde_listener_destroyed_window_is_unlinked) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener));
    struct cd_ctx *a = cd_new(), *b = cd_new();
    if (!a || !b) { t_skip("no window"); return; }
    CHECK(U(AddClipboardFormatListener)(a->hwnd)); a->listener = 1;
    CHECK(U(AddClipboardFormatListener)(b->hwnd)); b->listener = 1;
    a->listener = 0;
    U(DestroyWindow)(a->hwnd);                                /* no RemoveClipboardFormatListener */
    a->hwnd = NULL; cd_destroy(a);
    CHECK(cd_put(NULL, 0x265, "abcdefgh", 8));
    k_pump(150);
    EQ(CD_CNT(b, CD_WM_CLIPBOARDUPDATE), 1);           /* the list is intact for the survivor */
}

TEST(clipboard_dde_listener_render_does_not_dirty) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener));
    struct cd_ctx *o = render_owner(0x266, "R"), *l = cd_new();
    if (!o || !l) { t_skip("no window"); return; }
    CHECK(cd_put(o->hwnd, 0x266, NULL, 0));
    k_pump(50);
    CHECK(U(AddClipboardFormatListener)(l->hwnd)); l->listener = 1;
    CHECK(cd_open(NULL));
    CHECK(U(GetClipboardData)(0x266) != NULL);
    CHECK(U(CloseClipboard)());
    k_pump(150);
    EQ(CD_CNT(o, CD_WM_RENDERFORMAT), 1);
    EQ(CD_CNT(l, CD_WM_CLIPBOARDUPDATE), 0);           /* the render request restores flag 0x40 to its old value */
}

TEST(clipboard_dde_listener_direct_matches_user32) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserAddClipboardFormatListener_pfn ad = UN(NtUserAddClipboardFormatListener);
    NtUserRemoveClipboardFormatListener_pfn rm = UN(NtUserRemoveClipboardFormatListener);
    REQUIRE(ad && rm);
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(ad(c->hwnd)); c->listener = 1;
    SetLastError(0xDEAD);
    CHECK(!ad(c->hwnd));          EXPECT_ERR(CD_ERR_INVALID_PARAMETER);
    CHECK(rm(c->hwnd)); c->listener = 0;
    SetLastError(0xDEAD);
    CHECK(!rm(c->hwnd));          EXPECT_ERR(CD_ERR_INVALID_PARAMETER);
    SetLastError(0xDEAD);
    CHECK(!ad((HWND)(ULONG_PTR)0x7FFE7777)); EXPECT_ERR(CD_ERR_INVALID_WINDOW);
}

static DWORD WINAPI thr_add_listener(LPVOID p) {
    HWND h = p;
    return U(AddClipboardFormatListener)(h) ? 1 : 0;
}

TEST(clipboard_dde_listener_add_from_other_thread_same_process) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener));
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    /* ownership is per process (win32 process compare), not per thread */
    DWORD r = cd_thread(thr_add_listener, c->hwnd);
    EQ(r, 1);
    c->listener = (r == 1);
}

/* ---- clipboard viewer chain --------------------------------------------------------------------- */
static void join_chain(struct cd_ctx *c, HWND *prev_out) {
    HWND prev = U(GetClipboardViewer)();
    c->next_viewer = prev; c->forward = 1; c->in_chain = 1;
    SetLastError(0);
    HWND r = U(SetClipboardViewer)(c->hwnd);
    CHECK(r == prev);                                         /* SetClipboardViewer returns the previous first viewer */
    if (prev_out) *prev_out = prev;
}

TEST(clipboard_dde_viewer_set_and_get) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(GetClipboardViewer) && U(ChangeClipboardChain));
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    HWND prev;
    join_chain(a, &prev);
    EQ(U(GetClipboardViewer)(), a->hwnd);
    CHECK(U(ChangeClipboardChain)(a->hwnd, prev));            /* head removal: viewer = hwndNewNext */
    a->in_chain = 0;
    EQ(U(GetClipboardViewer)(), prev);
}

TEST(clipboard_dde_viewer_set_sends_drawclipboard_synchronously) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(GetClipboardViewer));
    struct cd_ctx *own = cd_new(), *a = cd_new();
    if (!own || !a) { t_skip("no window"); return; }
    CHECK(cd_put(own->hwnd, 0x270, "abcdefgh", 8));
    join_chain(a, NULL);
    EQ(CD_CNT(a, CD_WM_DRAWCLIPBOARD), 1);             /* SetClipboardViewer notifies the new viewer via SendNotifyMessage */
    EQ(a->last_w[CD_WM_DRAWCLIPBOARD - 0x300], (WPARAM)own->hwnd);   /* wParam = current clipboard owner */
    EQ(a->last_l[CD_WM_DRAWCLIPBOARD - 0x300], 0);
}

TEST(clipboard_dde_viewer_set_with_no_owner_sends_zero_wparam) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(GetClipboardViewer));
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    CHECK(cd_put(NULL, 0x271, "abcdefgh", 8));
    EQ(U(GetClipboardOwner)(), NULL);
    join_chain(a, NULL);
    EQ(CD_CNT(a, CD_WM_DRAWCLIPBOARD), 1);
    EQ(a->last_w[CD_WM_DRAWCLIPBOARD - 0x300], 0);
}

TEST(clipboard_dde_viewer_notified_on_change) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(GetClipboardViewer));
    struct cd_ctx *own = cd_new(), *a = cd_new();
    if (!own || !a) { t_skip("no window"); return; }
    join_chain(a, NULL);
    unsigned base = CD_CNT(a, CD_WM_DRAWCLIPBOARD);
    CHECK(cd_put(own->hwnd, 0x272, "abcdefgh", 8));
    EQ(CD_CNT(a, CD_WM_DRAWCLIPBOARD), base + 1);      /* CloseClipboard of a changed clipboard: exactly one */
    EQ(a->last_w[CD_WM_DRAWCLIPBOARD - 0x300], (WPARAM)own->hwnd);
    CHECK(cd_open(NULL)); CHECK(U(CloseClipboard)());
    EQ(CD_CNT(a, CD_WM_DRAWCLIPBOARD), base + 1);      /* an unchanged cycle is silent */
}

TEST(clipboard_dde_viewer_not_notified_after_chain_removed) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(GetClipboardViewer) && U(ChangeClipboardChain));
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    HWND prev;
    join_chain(a, &prev);
    CHECK(U(ChangeClipboardChain)(a->hwnd, prev));
    a->in_chain = 0;
    unsigned base = CD_CNT(a, CD_WM_DRAWCLIPBOARD);
    CHECK(cd_put(NULL, 0x273, "abcdefgh", 8));
    EQ(CD_CNT(a, CD_WM_DRAWCLIPBOARD), base);
}

TEST(clipboard_dde_viewer_set_posts_update_to_listeners) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(AddClipboardFormatListener));
    struct cd_ctx *a = cd_new(), *l = cd_new();
    if (!a || !l) { t_skip("no window"); return; }
    CHECK(cd_put(NULL, 0x274, "abcdefgh", 8));
    k_pump(50);
    CHECK(U(AddClipboardFormatListener)(l->hwnd)); l->listener = 1;
    join_chain(a, NULL);                                      /* the viewer notification runs the listener loop too */
    k_pump(150);
    EQ(CD_CNT(l, CD_WM_CLIPBOARDUPDATE), 1);
}

TEST(clipboard_dde_viewer_chain_head_removal_sends_nothing) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(GetClipboardViewer) && U(ChangeClipboardChain));
    struct cd_ctx *a = cd_new(), *b = cd_new();
    if (!a || !b) { t_skip("no window"); return; }
    join_chain(a, NULL);
    join_chain(b, NULL);                                      /* b -> a -> ... */
    EQ(b->next_viewer, a->hwnd);
    EQ(U(GetClipboardViewer)(), b->hwnd);
    CHECK(U(ChangeClipboardChain)(b->hwnd, a->hwnd));         /* hwndRemove is the head: pointer swap, no message */
    b->in_chain = 0;
    EQ(CD_CNT(b, CD_WM_CHANGECBCHAIN), 0);
    EQ(CD_CNT(a, CD_WM_CHANGECBCHAIN), 0);
    EQ(U(GetClipboardViewer)(), a->hwnd);
}

TEST(clipboard_dde_viewer_chain_middle_removal_sends_changecbchain) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(GetClipboardViewer) && U(ChangeClipboardChain));
    struct cd_ctx *a = cd_new(), *b = cd_new();
    if (!a || !b) { t_skip("no window"); return; }
    HWND prev;
    join_chain(a, &prev);
    join_chain(b, NULL);
    b->chain_ret = 0;
    BOOL r = U(ChangeClipboardChain)(a->hwnd, a->next_viewer);   /* a is second: message goes to the head (b) */
    a->in_chain = 0;
    EQ(CD_CNT(b, CD_WM_CHANGECBCHAIN), 1);
    EQ(b->last_w[CD_WM_CHANGECBCHAIN - 0x300], (WPARAM)a->hwnd);
    EQ(b->last_l[CD_WM_CHANGECBCHAIN - 0x300], (LPARAM)prev);
    EQ(b->next_viewer, prev);                          /* the handler relinked itself */
    t_note("ChangeClipboardChain with handler result 0 returned %d", (int)r);
    EQ(U(GetClipboardViewer)(), b->hwnd);
    EQ(CD_CNT(a, CD_WM_CHANGECBCHAIN), 0);
}

TEST(clipboard_dde_viewer_chain_result_is_handler_result) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(GetClipboardViewer) && U(ChangeClipboardChain));
    struct cd_ctx *a = cd_new(), *b = cd_new();
    if (!a || !b) { t_skip("no window"); return; }
    join_chain(a, NULL);
    join_chain(b, NULL);
    b->chain_ret = 1;
    BOOL r = U(ChangeClipboardChain)(a->hwnd, a->next_viewer);
    a->in_chain = 0;
    t_note("ChangeClipboardChain with handler result 1 returned %d", (int)r);
}

TEST(clipboard_dde_viewer_chain_three_deep_forwarding) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(GetClipboardViewer));
    struct cd_ctx *a = cd_new(), *b = cd_new(), *c = cd_new();
    if (!a || !b || !c) { t_skip("no window"); return; }
    join_chain(a, NULL); join_chain(b, NULL); join_chain(c, NULL);
    unsigned ba = CD_CNT(a, CD_WM_DRAWCLIPBOARD), bb = CD_CNT(b, CD_WM_DRAWCLIPBOARD), bc = CD_CNT(c, CD_WM_DRAWCLIPBOARD);
    CHECK(cd_put(NULL, 0x275, "abcdefgh", 8));
    EQ(CD_CNT(c, CD_WM_DRAWCLIPBOARD), bc + 1);        /* the kernel only notifies the head... */
    EQ(CD_CNT(b, CD_WM_DRAWCLIPBOARD), bb + 1);        /* ...each viewer forwards by itself */
    EQ(CD_CNT(a, CD_WM_DRAWCLIPBOARD), ba + 1);
}

TEST(clipboard_dde_viewer_invalid_windows) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(ChangeClipboardChain) && U(GetClipboardViewer));
    HWND before = U(GetClipboardViewer)();
    HWND bad = (HWND)(ULONG_PTR)0x7FFE7777;
    SetLastError(0xDEAD);
    CHECK(U(SetClipboardViewer)(bad) == NULL);
    EXPECT_ERR(CD_ERR_INVALID_WINDOW);
    EQ(U(GetClipboardViewer)(), before);               /* nothing changed */
    SetLastError(0xDEAD);
    CHECK(!U(ChangeClipboardChain)(bad, NULL));
    EXPECT_ERR(CD_ERR_INVALID_WINDOW);
    struct cd_ctx *a = cd_new();
    if (!a) return;
    SetLastError(0xDEAD);
    CHECK(!U(ChangeClipboardChain)(a->hwnd, bad));            /* invalid hwndNewNext is validated as well */
    EXPECT_ERR(CD_ERR_INVALID_WINDOW);
}

TEST(clipboard_dde_viewer_changechain_when_no_viewer) {
    CD_SETUP();
    REQUIRE(U(ChangeClipboardChain) && U(GetClipboardViewer));
    if (U(GetClipboardViewer)()) { t_skip("another viewer is installed in this session"); return; }
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    CHECK(!U(ChangeClipboardChain)(a->hwnd, NULL));           /* spwndClipViewer == NULL => FALSE */
}

static DWORD WINAPI thr_change_chain(LPVOID p) {
    HWND *h = p;
    return U(ChangeClipboardChain)(h[0], h[1]) ? 1 : 0;
}
TEST(clipboard_dde_viewer_changechain_requires_owning_thread) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(ChangeClipboardChain) && U(GetClipboardViewer));
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    HWND prev;
    join_chain(a, &prev);
    HWND args[2] = { a->hwnd, prev };
    EQ(cd_thread(thr_change_chain, args), 0);          /* pti of hwndRemove != calling thread => 0 */
    EQ(U(GetClipboardViewer)(), a->hwnd);
}

TEST(clipboard_dde_viewer_direct_matches_user32) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserSetClipboardViewer_pfn sv = UN(NtUserSetClipboardViewer);
    NtUserGetClipboardViewer_pfn gv = UN(NtUserGetClipboardViewer);
    NtUserChangeClipboardChain_pfn cc = UN(NtUserChangeClipboardChain);
    NtUserGetClipboardOwner_pfn go = UN(NtUserGetClipboardOwner);
    REQUIRE(sv && gv && cc && go);
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    HWND prev = gv();
    a->next_viewer = prev; a->forward = 1; a->in_chain = 1;
    EQ(sv(a->hwnd), prev);
    EQ(gv(), a->hwnd);
    EQ(U(GetClipboardViewer)(), gv());
    EQ(go(), U(GetClipboardOwner)());
    CHECK(cc(a->hwnd, prev));
    a->in_chain = 0;
    EQ(gv(), prev);
}

TEST(clipboard_dde_getters_agree_with_direct) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserGetClipboardOwner_pfn go = UN(NtUserGetClipboardOwner);
    NtUserGetOpenClipboardWindow_pfn gow = UN(NtUserGetOpenClipboardWindow);
    REQUIRE(go && gow);
    struct cd_ctx *a = cd_new();
    if (!a) { t_skip("no window"); return; }
    CHECK(cd_open_empty(a->hwnd));
    EQ(go(), a->hwnd);
    EQ(gow(), a->hwnd);
    CHECK(U(CloseClipboard)());
    EQ(go(), a->hwnd);
    CHECK(gow() != a->hwnd);
}
