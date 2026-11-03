/* clipboard_dde: standard-format table, kernel handle accounting, and the process access checks. */
#include "cd.h"

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* number of live TYPE_CLIPDATA entries in the USER handle table (entry type byte at +0x18) */
static int count_clipdata(void) {
    struct k_user_shared_info si;
    unsigned long long n = 0;
    if (!k_shared_info(&si) || !si.psi || !si.aheList) return -1;
    if (!k_try_read((char *)si.psi + 8, &n, 8) || n == 0 || n > 0x10000) return -1;
    unsigned es = si.heEntrySize ? si.heEntrySize : 0x20;
    int cnt = 0;
    unsigned char *buf = malloc((size_t)es * 256);
    if (!buf) return -1;
    for (unsigned long long i = 0; i < n; i += 256) {
        unsigned c = (unsigned)(n - i < 256 ? n - i : 256);
        if (!k_try_read((char *)si.aheList + i * es, buf, (size_t)c * es)) { free(buf); return -1; }
        for (unsigned j = 0; j < c; j++) if (buf[(size_t)j * es + 0x18] == CD_TYPE_CLIPDATA && !(buf[(size_t)j * es + 0x19] & 1)) cnt++;
    }
    free(buf);
    return cnt;
}

TEST(clipboard_dde_standard_formats_accept_delayed_entries) {
    CD_SETUP();
    static const UINT fmts[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                                 0x80, 0x81, 0x82, 0x83, 0x8E };
    CHECK(cd_open_empty(NULL));
    for (int i = 0; i < N(fmts); i++) {
        U(SetClipboardData)(fmts[i], NULL);                   /* hData 0: any numeric format may be announced */
        CHECKF(U(IsClipboardFormatAvailable)(fmts[i]), "format 0x%x", fmts[i]);
        CHECKF(U(CountClipboardFormats)() == i + 1, "count after 0x%x", fmts[i]);
    }
    UINT out[32];
    int n = cd_formats(out, 32);
    EQ(n, N(fmts));
    for (int i = 0; i < n && i < N(fmts); i++) EQ(out[i], fmts[i]);
    for (int i = 0; i < N(fmts); i++) {
        SetLastError(0);
        CHECKF(U(GetClipboardData)(fmts[i]) == NULL, "format 0x%x", fmts[i]);   /* no owner => cannot render */
    }
    U(CloseClipboard)();
}

TEST(clipboard_dde_standard_formats_absent_after_empty) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    CHECK(U(CloseClipboard)());
    for (UINT f = 0; f <= 0x20; f++) CHECKF(!U(IsClipboardFormatAvailable)(f), "format 0x%x", f);
    EQ(U(CountClipboardFormats)(), 0);
}

TEST(clipboard_dde_clipdata_objects_follow_clipboard_contents) {
    CD_SETUP();
    int base = count_clipdata();
    if (base < 0) { t_skip("USER handle table unreadable"); return; }
    CHECK(cd_open_empty(NULL));
    int before = count_clipdata();
    for (UINT i = 0; i < 6; i++) {
        char d[8] = "01234567";
        HGLOBAL g = cd_gmem(d, 8);
        CHECK(U(SetClipboardData)(0x2C0 + i, g) == g);
    }
    int during = count_clipdata();
    CHECK(during >= before + 6);                              /* one TYPE_CLIPDATA object per global-memory entry */
    CHECK(U(EmptyClipboard)());
    int after = count_clipdata();
    CHECK(after <= during - 6);
    U(CloseClipboard)();
}

TEST(clipboard_dde_clipdata_objects_not_leaked_by_cycles) {
    CD_SETUP();
    int c0 = count_clipdata();
    if (c0 < 0) { t_skip("USER handle table unreadable"); return; }
    for (int i = 0; i < 25; i++) CHECK(cd_put(NULL, 0x2D0, "leakcheck", 10));
    CHECK(cd_open_empty(NULL)); U(CloseClipboard)();
    int c1 = count_clipdata();
    t_note("TYPE_CLIPDATA objects: %d before, %d after 25 set/empty cycles", c0, c1);
    CHECK(c1 - c0 < 10);                                      /* each replaced/emptied entry destroys its object */
}

TEST(clipboard_dde_synthesized_text_creates_clipdata_per_materialization) {
    CD_SETUP();
    CHECK(cd_put_text(NULL, L"counting"));
    int c0 = count_clipdata();
    if (c0 < 0) { t_skip("USER handle table unreadable"); return; }
    CHECK(cd_open(NULL));
    CHECK(U(GetClipboardData)(CF_TEXT) != NULL);              /* NtUserConvertMemHandle for the converted copy */
    int c1 = count_clipdata();
    CHECK(c1 >= c0 + 1);
    CHECK(U(GetClipboardData)(CF_TEXT) != NULL);              /* cached: nothing new */
    EQ(count_clipdata(), c1);
    U(CloseClipboard)();
}

TEST(clipboard_dde_handle_type_of_returned_kernel_handles) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserGetClipboardData_pfn gt = UN(NtUserGetClipboardData);
    REQUIRE(gt);
    CHECK(cd_put(NULL, 0x2C8, "typecheck", 10));
    CHECK(cd_open(NULL));
    CD_GETCLIPBDATA o = {0};
    HANDLE h = gt(0x2C8, &o);
    CHECK(h != NULL);
    struct k_user_entry e;
    if (h && k_user_entry(h, &e)) {
        EQ(e.type, CD_TYPE_CLIPDATA);
        EQ(e.uniq, (unsigned short)((ULONG_PTR)h >> 16));
        CHECK(!(e.flags & 1));
    } else t_note("handle table entry unreadable");
    U(CloseClipboard)();
}

/* ---- NtUserCheckProcessForClipboardAccess / NtUserGetClipboardAccessToken --------------------- */
TEST(clipboard_dde_check_process_access_self) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserCheckProcessForClipboardAccess_pfn f = UN(NtUserCheckProcessForClipboardAccess);
    REQUIRE(f);
    DWORD out = 0x55;
    CHECK(f(GetCurrentProcessId(), &out, NULL));               /* LockProcessByClientId + a Win32 process: returns 1 */
    CHECK(out == 0 || out == 1);
    t_note("clipboard foreground-access verdict for this process: %lu", out);
}

TEST(clipboard_dde_check_process_access_invalid_pid) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserCheckProcessForClipboardAccess_pfn f = UN(NtUserCheckProcessForClipboardAccess);
    REQUIRE(f);
    static const DWORD pids[] = { 0xFFFFFFF0, 0x7FFFFFF8, 4 };  /* no such process / the System process has no Win32 state */
    for (int i = 0; i < N(pids); i++) {
        DWORD out = 0x55;
        SetLastError(0xDEAD);
        CHECKF(!f(pids[i], &out, NULL), "pid %lu", pids[i]);
        EXPECT_ERR(CD_ERR_INVALID_PARAMETER);
        EQ(out, 0x55);
    }
}

TEST(clipboard_dde_access_token_observed) {
    CD_SETUP(); CD_NEED_WIN32K();
    typedef BOOL (WINAPI *tok_fn)(HANDLE *, DWORD);
    tok_fn f = (tok_fn)t_proc_in("win32u.dll", "NtUserGetClipboardAccessToken");
    REQUIRE(f);
    HANDLE tok = (HANDLE)0x1234;
    SetLastError(0);
    BOOL r = f(&tok, TOKEN_QUERY);
    t_note("NtUserGetClipboardAccessToken -> %d err %lu token %p", r, GetLastError(), tok);
    if (r && tok && tok != (HANDLE)0x1234) CloseHandle(tok);
}
