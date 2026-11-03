/* gap9_clip*: NtUserGetUpdatedClipboardFormats, NtUserAddClipboardFormatListener
 * (and the matching Remove).  Registration of a listener on a private hidden window is undone before each test ends.
 * Only the two exact-fit format-list tests put content on the clipboard (one text item, emptied again before they end).
 * Other content-changing cases live in dx_gap_09.
 *
 * Analysis (win32kfull):
 *  - GetUpdatedClipboardFormats(pFormats, cFormats, pcOut): writes *pcOut = number of formats visible at the
 *    caller's integrity level BEFORE the size check; cFormats < count -> LastError 0x7A (122), return 0 and no
 *    entry is written; otherwise at most cFormats entries are stored, return 1, last error untouched.
 *  - AddClipboardFormatListener(hwnd): ValidateHwnd failure -> 0 (1400); window of another process -> 5;
 *    window already listening (state2 bit 0) -> 0x57; otherwise sets the bit.
 *  - RemoveClipboardFormatListener(hwnd): window without the bit -> 0x57, else clears it. */
#include "g9.h"

UDECL(BOOL, NtUserGetUpdatedClipboardFormats, (UINT *fmts, UINT c, UINT *pcOut));
UDECL(BOOL, NtUserAddClipboardFormatListener, (HWND h));
UDECL(BOOL, NtUserRemoveClipboardFormatListener, (HWND h));

#define SENT 0xDEADBEEFu
#define NBUF 64

static void fill(UINT *b, int n) { for (int i = 0; i < n; i++) b[i] = SENT; }
static int all_sent(const UINT *b, int from, int to) { for (int i = from; i < to; i++) if (b[i] != SENT) return 0; return 1; }

/* Gives the clipboard one text item (several synthesized formats follow) so the format-list tests have something to list;
 * g9_clip_end empties it again. Returns the owner window, or NULL when the clipboard could not be filled. */
static HWND g9_clip_begin(void) {
    HWND w = g9_window(0);
    if (!w) return NULL;
    int ok = 0;
    if (U(OpenClipboard)(w)) {
        if (U(EmptyClipboard)()) {
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 8 * sizeof(WCHAR));
            if (g) {
                WCHAR *p = (WCHAR *)GlobalLock(g);
                if (p) { lstrcpyW(p, L"win32kt"); GlobalUnlock(g); ok = U(SetClipboardData)(CF_UNICODETEXT, g) != NULL; }
                if (!ok) GlobalFree(g);
            }
        }
        U(CloseClipboard)();
    }
    if (!ok) { g9_destroy(w); return NULL; }
    return w;
}
static void g9_clip_end(HWND w) {
    if (!w) return;
    if (U(OpenClipboard)(w)) { U(EmptyClipboard)(); U(CloseClipboard)(); }
    g9_destroy(w);
}

/* Count seen by a size query; returns -1 when the query itself misbehaves. */
static int query_count(BOOL (WINAPI *fn)(UINT *, UINT, UINT *)) {
    UINT n = SENT;
    BOOL r;
    SetLastError(0);
    r = fn(NULL, 0, &n);
    if (n == SENT) return -1;
    if (n == 0) { CHECK(r); } else { CHECK(!r); EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER); }
    return (int)n;
}

TEST(gap9_clipfmt_size_query_direct) {
    REQUIRE(UN(NtUserGetUpdatedClipboardFormats));
    int n = query_count(UN(NtUserGetUpdatedClipboardFormats));
    CHECK(n >= 0);            /* *pcOut is stored before the 0x7A decision */
    t_note("updated clipboard format count = %d", n);
}

TEST(gap9_clipfmt_size_query_user32) {
    REQUIRE(U(GetUpdatedClipboardFormats));
    /* user32!GetUpdatedClipboardFormats is a bare jump to the syscall */
    int n = query_count(U(GetUpdatedClipboardFormats));
    CHECK(n >= 0);
}

TEST(gap9_clipfmt_wrapper_equals_syscall) {
    REQUIRE(U(GetUpdatedClipboardFormats)); REQUIRE(UN(NtUserGetUpdatedClipboardFormats));
    int a = query_count(U(GetUpdatedClipboardFormats)), b = query_count(UN(NtUserGetUpdatedClipboardFormats));
    EXPECT_EQ(a, b);
}

TEST(gap9_clipfmt_exact_fit) {
    REQUIRE(UN(NtUserGetUpdatedClipboardFormats)); REQUIRE(U(OpenClipboard));
    HWND ow = g9_clip_begin();
    int n = query_count(UN(NtUserGetUpdatedClipboardFormats));
    if (n <= 0 || n > NBUF - 4) { g9_clip_end(ow); t_skip("clipboard has %d formats", n); return; }
    UINT buf[NBUF], cnt = SENT;
    fill(buf, NBUF);
    SetLastError(0x5555);
    BOOL r = UN(NtUserGetUpdatedClipboardFormats)(buf, n, &cnt);
    CHECK(r);
    EXPECT_EQ(cnt, n);
    EXPECT_ERR(0x5555);                      /* success path never calls UserSetLastError */
    for (int i = 0; i < n; i++) CHECKF(buf[i] != SENT && buf[i] != 0, "entry %d = 0x%x", i, buf[i]);
    CHECK(all_sent(buf, n, NBUF));           /* exactly cFormats slots are the limit */
    g9_clip_end(ow);
}

TEST(gap9_clipfmt_one_short_writes_nothing) {
    REQUIRE(UN(NtUserGetUpdatedClipboardFormats)); REQUIRE(U(OpenClipboard));
    HWND ow = g9_clip_begin();
    int n = query_count(UN(NtUserGetUpdatedClipboardFormats));
    if (n <= 0 || n > NBUF - 4) { g9_clip_end(ow); t_skip("clipboard has %d formats", n); return; }
    UINT buf[NBUF], cnt = SENT;
    fill(buf, NBUF);
    SetLastError(0);
    BOOL r = UN(NtUserGetUpdatedClipboardFormats)(buf, n - 1, &cnt);
    CHECK(!r);
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    EXPECT_EQ(cnt, n);                       /* the required count is still reported */
    CHECK(all_sent(buf, 0, NBUF));           /* size check precedes the copy loop */
    g9_clip_end(ow);
}

TEST(gap9_clipfmt_zero_capacity_ignores_buffer) {
    REQUIRE(UN(NtUserGetUpdatedClipboardFormats));
    int n = query_count(UN(NtUserGetUpdatedClipboardFormats));
    if (n < 0) { t_skip("query failed"); return; }
    UINT buf[8], cnt = SENT;
    fill(buf, 8);
    BOOL r = UN(NtUserGetUpdatedClipboardFormats)(buf, 0, &cnt);
    EXPECT_EQ(cnt, n);
    EXPECT_EQ(r, n == 0);
    CHECK(all_sent(buf, 0, 8));
}

TEST(gap9_clipfmt_oversized_capacity_reports_actual_count) {
    REQUIRE(UN(NtUserGetUpdatedClipboardFormats));
    int n = query_count(UN(NtUserGetUpdatedClipboardFormats));
    if (n < 0 || n > 40) { t_skip("n=%d", n); return; }
    UINT buf[NBUF], cnt = SENT;
    fill(buf, NBUF);
    CHECK(UN(NtUserGetUpdatedClipboardFormats)(buf, NBUF, &cnt));
    EXPECT_EQ(cnt, n);                       /* *pcOut = count of formats, not the capacity */
    CHECK(all_sent(buf, n, NBUF));
}

TEST(gap9_clipfmt_registered_ids_have_names) {
    REQUIRE(UN(NtUserGetUpdatedClipboardFormats)); REQUIRE(U(GetClipboardFormatNameW));
    UINT buf[NBUF], cnt = 0;
    fill(buf, NBUF);
    if (!UN(NtUserGetUpdatedClipboardFormats)(buf, NBUF, &cnt)) { t_skip("too many formats"); return; }
    for (UINT i = 0; i < cnt; i++) {
        if (buf[i] >= 0xC000 && buf[i] <= 0xFFFF) {
            WCHAR nm[128];
            CHECKF(U(GetClipboardFormatNameW)(buf[i], nm, 128) > 0, "registered id 0x%x has no name", buf[i]);
        }
        CHECKF(buf[i] != 0, "entry %u is zero", i);
    }
}

static DWORD thr_count(void *p) {
    int *o = (int *)p;
    *o = query_count(UN(NtUserGetUpdatedClipboardFormats));
    return 0;
}
TEST(gap9_clipfmt_from_second_thread_same_count) {
    REQUIRE(UN(NtUserGetUpdatedClipboardFormats));
    int a = query_count(UN(NtUserGetUpdatedClipboardFormats)), b = -2;
    g9_on_thread(thr_count, &b);
    EXPECT_EQ(a, b);                         /* the list belongs to the window station, not the thread */
}

/* ---- listener registration -------------------------------------------------------------------- */
TEST(gap9_clipl_add_remove_cycle) {
    REQUIRE(UN(NtUserAddClipboardFormatListener)); REQUIRE(UN(NtUserRemoveClipboardFormatListener));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    SetLastError(0);
    CHECK(UN(NtUserAddClipboardFormatListener)(h));
    SetLastError(0);
    CHECK(!UN(NtUserAddClipboardFormatListener)(h));       /* bit already set -> 0x57 */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(UN(NtUserRemoveClipboardFormatListener)(h));
    SetLastError(0);
    CHECK(!UN(NtUserRemoveClipboardFormatListener)(h));    /* bit clear -> 0x57 */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(UN(NtUserAddClipboardFormatListener)(h));        /* re-registration after removal */
    CHECK(UN(NtUserRemoveClipboardFormatListener)(h));
    g9_destroy(h);
}

TEST(gap9_clipl_never_added_remove_fails) {
    REQUIRE(UN(NtUserRemoveClipboardFormatListener));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    SetLastError(0);
    CHECK(!UN(NtUserRemoveClipboardFormatListener)(h));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    g9_destroy(h);
}

TEST(gap9_clipl_invalid_handles) {
    REQUIRE(UN(NtUserAddClipboardFormatListener)); REQUIRE(UN(NtUserRemoveClipboardFormatListener));
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x12345, (HWND)(ULONG_PTR)0xFFFFFFFF, (HWND)(ULONG_PTR)-1 };
    for (int i = 0; i < G9_N(bad); i++) {
        SetLastError(0);
        CHECKF(!UN(NtUserAddClipboardFormatListener)(bad[i]), "add %p", bad[i]);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
        SetLastError(0);
        CHECKF(!UN(NtUserRemoveClipboardFormatListener)(bad[i]), "remove %p", bad[i]);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    }
}

TEST(gap9_clipl_destroyed_window_handle) {
    REQUIRE(UN(NtUserAddClipboardFormatListener)); REQUIRE(UN(NtUserRemoveClipboardFormatListener));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    CHECK(UN(NtUserAddClipboardFormatListener)(h));
    g9_destroy(h);                                         /* DestroyWindow unlinks the listener itself */
    SetLastError(0);
    CHECK(!UN(NtUserRemoveClipboardFormatListener)(h));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    SetLastError(0);
    CHECK(!UN(NtUserAddClipboardFormatListener)(h));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}

TEST(gap9_clipl_foreign_process_window) {
    REQUIRE(UN(NtUserAddClipboardFormatListener)); REQUIRE(UN(NtUserRemoveClipboardFormatListener)); REQUIRE(U(GetDesktopWindow));
    HWND d = U(GetDesktopWindow)();
    /* the desktop window belongs to the system process: both calls stop at the process comparison, which returns the
     * constant 5 as the (truthy) result without setting a last error and without registering anything */
    SetLastError(0);
    EXPECT_EQ(UN(NtUserAddClipboardFormatListener)(d), 5);
    EXPECT_ERR(0);
    SetLastError(0);
    EXPECT_EQ(UN(NtUserRemoveClipboardFormatListener)(d), 5);
    EXPECT_ERR(0);
}

static DWORD thr_add_remove(void *p) {
    HWND h = (HWND)p;
    DWORD ok = 0;
    SetLastError(0);
    if (UN(NtUserAddClipboardFormatListener)(h)) ok |= 1;
    if (UN(NtUserRemoveClipboardFormatListener)(h)) ok |= 2;
    return ok;
}
TEST(gap9_clipl_other_thread_same_process) {
    REQUIRE(UN(NtUserAddClipboardFormatListener)); REQUIRE(UN(NtUserRemoveClipboardFormatListener));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    /* only the PROCESS of the window's thread is compared, not the thread */
    EXPECT_EQ(g9_on_thread(thr_add_remove, h), 3);
    g9_destroy(h);
}

TEST(gap9_clipl_user32_wrappers_match) {
    REQUIRE(U(AddClipboardFormatListener)); REQUIRE(U(RemoveClipboardFormatListener));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    CHECK(U(AddClipboardFormatListener)(h));
    SetLastError(0);
    CHECK(!U(AddClipboardFormatListener)(h));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(U(RemoveClipboardFormatListener)(h));
    g9_destroy(h);
}

TEST(gap9_clipl_two_windows_independent) {
    REQUIRE(UN(NtUserAddClipboardFormatListener)); REQUIRE(UN(NtUserRemoveClipboardFormatListener));
    HWND a = g9_window(0), b = g9_window(0);
    if (!a || !b) { t_skip("no window"); g9_destroy(a); g9_destroy(b); return; }
    CHECK(UN(NtUserAddClipboardFormatListener)(a));
    CHECK(UN(NtUserAddClipboardFormatListener)(b));        /* the flag is per window */
    CHECK(UN(NtUserRemoveClipboardFormatListener)(a));
    SetLastError(0);
    CHECK(!UN(NtUserRemoveClipboardFormatListener)(a));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(UN(NtUserRemoveClipboardFormatListener)(b));
    g9_destroy(a); g9_destroy(b);
}
