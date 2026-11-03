/* dx_process_session_cleanup: clipboard ownership and the open-clipboard lock when the owning thread or
 * process dies. These tests EMPTY the system clipboard (best effort: CF_UNICODETEXT is saved and restored),
 * hence they run only with /destructive.
 *
 * win32kfull: a dying thread/process closes the clipboard it holds open (during
 * thread/process teardown), and the owner window disappearing clears the owner
 * (GetClipboardOwner -> NULL); the data a window put on the clipboard already rendered stays.
 */
#include "../process_session_cleanup/psc.h"
#include "../process_session_cleanup/psc.c"

static wchar_t *saved_text;
static void clip_save(void) {
    saved_text = NULL;
    if (!U(OpenClipboard)(NULL)) return;
    HANDLE h = U(GetClipboardData)(CF_UNICODETEXT);
    if (h) { const wchar_t *p = (const wchar_t *)GlobalLock(h); if (p) { saved_text = _wcsdup(p); GlobalUnlock(h); } }
    U(CloseClipboard)();
}
static void clip_restore(void) {
    if (!U(OpenClipboard)(NULL)) return;
    U(EmptyClipboard)();
    if (saved_text) {
        size_t n = (wcslen(saved_text) + 1) * sizeof(wchar_t);
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
        if (g) { void *p = GlobalLock(g); memcpy(p, saved_text, n); GlobalUnlock(g); if (!U(SetClipboardData)(CF_UNICODETEXT, g)) GlobalFree(g); }
        free(saved_text); saved_text = NULL;
    }
    U(CloseClipboard)();
}
#define NEED_CLIP() do { REQUIRE(U(OpenClipboard)); REQUIRE(U(CreateWindowExA)); HWND p_ = psc_mk(NULL, "p"); if (!p_) { t_skip("no window"); return; } U(DestroyWindow)(p_); } while (0)

struct clip_ctx { UINT fr, fd; int keep_open; };
static void clip_fn(struct psc_th *t) {
    struct clip_ctx *c = (struct clip_ctx *)t->user;
    HWND w = psc_mk(NULL, "clip");
    t->hw[0] = w;
    if (U(OpenClipboard)(w)) {
        U(EmptyClipboard)();
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 16);
        if (g) { char *p = (char *)GlobalLock(g); memset(p, 0x41, 16); GlobalUnlock(g); if (!U(SetClipboardData)(c->fr, g)) GlobalFree(g); }
        U(SetClipboardData)(c->fd, NULL);          /* delayed rendering */
        t->r[0] = 1;
        if (!c->keep_open) U(CloseClipboard)();
    }
    t->ok = 1;
    psc_park(t);
}
static void thread_case(int keep_open, int destroy_first) {
    NEED_CLIP();
    clip_save();
    struct clip_ctx c;
    char n1[64], n2[64];
    sprintf(n1, "win32kt_psc_fr_%lu", GetCurrentProcessId()); sprintf(n2, "win32kt_psc_fd_%lu", GetCurrentProcessId());
    c.fr = U(RegisterClipboardFormatA)(n1); c.fd = U(RegisterClipboardFormatA)(n2); c.keep_open = keep_open;
    struct psc_th t;
    REQUIRE(psc_th_start(&t, clip_fn, &c));
    if (!t.r[0]) { t_skip("worker could not open the clipboard (%lu)", GetLastError()); psc_th_end(&t); clip_restore(); return; }
    HWND w = t.hw[0];
    if (keep_open) {
        SetLastError(PSC_MAGIC);
        CHECK(!U(OpenClipboard)(NULL));
        EXPECT_ERR(ERROR_ACCESS_DENIED);                       /* another thread holds the clipboard open */
        CHECK(U(GetOpenClipboardWindow)() == w);
    } else {
        CHECK(U(GetClipboardOwner)() == w);
        CHECK(U(IsClipboardFormatAvailable)(c.fr));
        CHECK(U(IsClipboardFormatAvailable)(c.fd));            /* a delayed-render format is listed as available */
    }
    if (destroy_first) { /* the thread keeps living; only its window goes: done via a message handled in-thread */
        CHECK(U(PostMessageA)(w, WM_CLOSE, 0, 0));
    }
    CHECK(psc_th_end(&t));
    CHECK(PSC_POLL(U(GetOpenClipboardWindow)() == NULL, 4000));
    BOOL ok = FALSE;
    CHECKF(PSC_POLL((ok = U(OpenClipboard)(NULL)) != 0, 4000), "clipboard still locked after the owner thread died (%lu)", GetLastError());
    if (ok) {
        CHECK(U(GetClipboardOwner)() == NULL);                 /* owner window destroyed with the thread */
        t_note("rendered format available after owner thread death: %d, delayed: %d", U(IsClipboardFormatAvailable)(c.fr), U(IsClipboardFormatAvailable)(c.fd));
        U(CloseClipboard)();
    }
    clip_restore();
}
TEST(dx_process_session_cleanup_clip_thread_exit_releases_owner) { thread_case(0, 0); }
TEST(dx_process_session_cleanup_clip_thread_exit_closes_open_clipboard) { thread_case(1, 0); }

SCEN(clipowner) {                          /* extra = "open" keeps the clipboard open while the parent inspects */
    HWND w = psc_mk(NULL, "clipowner");
    char n1[64], n2[64];
    sprintf(n1, "win32kt_psc_fr_%lu", x->ppid); sprintf(n2, "win32kt_psc_fd_%lu", x->ppid);
    UINT fr = U(RegisterClipboardFormatA)(n1), fd = U(RegisterClipboardFormatA)(n2);
    CC(w && U(OpenClipboard)(w));
    U(EmptyClipboard)();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 16);
    if (g) { char *p = (char *)GlobalLock(g); memset(p, 0x42, 16); GlobalUnlock(g); CC(U(SetClipboardData)(fr, g) != NULL); }
    U(SetClipboardData)(fd, NULL);
    if (strcmp(x->extra, "open")) U(CloseClipboard)();
    x->s->v[0] = (LONGLONG)(ULONG_PTR)w;
    psc_sync_pump(x);
}
static void proc_case(int keep_open, int terminate) {
    NEED_CLIP();
    clip_save();
    struct psc_ch c;
    if (!psc_spawn(&c, "clipowner", keep_open ? "open" : "closed") || !psc_wait(&c, PSC_WAIT)) { t_skip("child did not start"); psc_free(&c); clip_restore(); return; }
    HWND w = (HWND)(ULONG_PTR)c.s->v[0];
    char n1[64], n2[64];
    sprintf(n1, "win32kt_psc_fr_%lu", GetCurrentProcessId()); sprintf(n2, "win32kt_psc_fd_%lu", GetCurrentProcessId());
    UINT fr = U(RegisterClipboardFormatA)(n1), fd = U(RegisterClipboardFormatA)(n2);
    if (keep_open) {
        CHECK(U(GetOpenClipboardWindow)() == w);
        SetLastError(PSC_MAGIC);
        CHECK(!U(OpenClipboard)(NULL));
        EXPECT_ERR(ERROR_ACCESS_DENIED);
    } else {
        CHECK(U(GetClipboardOwner)() == w);
        CHECK(U(IsClipboardFormatAvailable)(fr));
        CHECK(U(IsClipboardFormatAvailable)(fd));
    }
    if (terminate) TerminateProcess(c.hp, 5); else psc_go(&c);
    CHECK(WaitForSingleObject(c.hp, PSC_WAIT) == WAIT_OBJECT_0);
    CHECK(PSC_POLL(U(GetOpenClipboardWindow)() == NULL, 5000));
    BOOL ok = FALSE;
    CHECKF(PSC_POLL((ok = U(OpenClipboard)(NULL)) != 0, 5000), "clipboard still locked after the owner process ended (%lu)", GetLastError());
    if (ok) {
        CHECK(U(GetClipboardOwner)() == NULL);
        t_note("rendered format available after owner process death: %d, delayed: %d", U(IsClipboardFormatAvailable)(fr), U(IsClipboardFormatAvailable)(fd));
        U(CloseClipboard)();
    }
    psc_free(&c);
    clip_restore();
}
TEST(dx_process_session_cleanup_clip_process_exit_releases_owner) { proc_case(0, 0); }
TEST(dx_process_session_cleanup_clip_process_terminated_releases_owner) { proc_case(0, 1); }
TEST(dx_process_session_cleanup_clip_process_terminated_with_clipboard_open) { proc_case(1, 1); }
TEST(dx_process_session_cleanup_clip_process_exit_with_clipboard_open) { proc_case(1, 0); }
