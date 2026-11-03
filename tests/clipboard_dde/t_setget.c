/* clipboard_dde: SetClipboardData / GetClipboardData, the sequence number, and the TYPE_CLIPDATA
 * kernel objects that carry global-memory clipboard data.
 *
 * user32!SetClipboardData converts an HGLOBAL with ConvertMemHandle -> NtUserConvertMemHandle
 * (the kernel allocates a clipboard data object and copies the bytes in) and calls
 * NtUserSetClipboardData(fmt, kernelHandle, {fGlobalHandle, fIncSerial}); it keeps a
 * {kernelHandle, hMem, format} node list (gphn) so that GetClipboardData in the same process
 * returns the caller's own HGLOBAL. Other processes get a fresh HGLOBAL built by
 * NtUserCreateLocalMemHandle. the kernel destroys the TYPE_CLIPDATA object when the entry is
 * replaced or the clipboard is emptied. */
#include "cd.h"

static int cd_clip_type(HANDLE h, unsigned char *type, unsigned short *uniq) {
    struct k_user_entry e;
    if (!k_user_entry(h, &e)) return 0;
    if (type) *type = e.type;
    if (uniq) *uniq = e.uniq;
    return 1;
}

static void fill(unsigned char *p, size_t n, unsigned seed) {
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char)((i * 131u + seed) ^ (i >> 7));
}

/* ---- basic round trips --------------------------------------------------------------------- */
TEST(clipboard_dde_set_text_returns_same_handle_and_get_matches) {
    CD_SETUP();
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    HGLOBAL g = cd_gstr_w(L"hello clipboard");
    CHECK(cd_open_empty(c->hwnd));
    EQ(U(SetClipboardData)(CF_UNICODETEXT, g), g);      /* user32 returns hMem on success */
    EQ(U(GetClipboardData)(CF_UNICODETEXT), g);          /* gphn node maps the kernel handle back to hMem */
    CHECK(U(CloseClipboard)());
    CHECK(cd_open(c->hwnd));                                    /* owner is in this process: node list is kept */
    EQ(U(GetClipboardData)(CF_UNICODETEXT), g);
    WCHAR b[32] = {0};
    CHECK(cd_get_bytes(CF_UNICODETEXT, b, sizeof b) >= sizeof L"hello clipboard");
    EXPECT_WSTR(b, L"hello clipboard");
    U(CloseClipboard)();
}

TEST(clipboard_dde_set_returns_hmem_for_hglobal_formats) {
    CD_SETUP();
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    static const UINT fmts[] = { CF_TEXT, CF_OEMTEXT, CF_UNICODETEXT, CF_DSPTEXT, 0x1FF, 0x200, 0x201, 0x2FF, 0x300, 0x3FF, 0x400, 0x7FFF };
    for (unsigned i = 0; i < sizeof fmts / sizeof fmts[0]; i++) {
        char data[24] = "payload-0123456789";
        HGLOBAL g = cd_gmem(data, sizeof data);
        CHECK(cd_open_empty(c->hwnd));
        HANDLE r = U(SetClipboardData)(fmts[i], g);
        CHECKF(r == g, "format 0x%x", fmts[i]);
        CHECKF(U(IsClipboardFormatAvailable)(fmts[i]), "format 0x%x", fmts[i]);
        CHECKF(U(GetClipboardData)(fmts[i]) == g, "format 0x%x", fmts[i]);
        U(CloseClipboard)();
    }
}

TEST(clipboard_dde_set_text_nul_terminates_hglobal) {
    CD_SETUP();
    /* ConvertMemHandle(h, 1) zeroes the last byte of the caller's HGLOBAL before the kernel copy */
    HGLOBAL g = cd_gmem("ABCD", 4);
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_TEXT, g) == g);
    char *p = GlobalLock(g);
    CHECK(p != NULL);
    if (p) { CHECK(!memcmp(p, "ABC\0", 4)); GlobalUnlock(g); }
    U(CloseClipboard)();
}

TEST(clipboard_dde_set_unicode_nul_terminates_hglobal) {
    CD_SETUP();
    WCHAR w[4] = { 'A', 'B', 'C', 'D' };
    HGLOBAL g = cd_gmem(w, sizeof w);
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_UNICODETEXT, g) == g);
    WCHAR *p = GlobalLock(g);
    CHECK(p != NULL);
    if (p) { CHECK(p[0] == 'A' && p[1] == 'B' && p[2] == 'C' && p[3] == 0); GlobalUnlock(g); }
    WCHAR out[8] = {0};
    CHECK(cd_get_bytes(CF_UNICODETEXT, out, sizeof out) >= sizeof w);
    EXPECT_WSTR(out, L"ABC");
    U(CloseClipboard)();
}

TEST(clipboard_dde_set_same_format_frees_previous_hglobal) {
    CD_SETUP();
    HGLOBAL g1 = cd_gstr_w(L"first");
    HGLOBAL g2 = cd_gstr_w(L"second");
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_UNICODETEXT, g1) == g1);
    CHECK(!cd_freed(g1));
    CHECK(U(SetClipboardData)(CF_UNICODETEXT, g2) == g2);
    /* SetClipboardData -> DeleteClientClipboardHandle(node) -> GlobalFree(old hMem) */
    CHECK(cd_freed(g1));
    CHECK(!cd_freed(g2));
    WCHAR b[16] = {0};
    CHECK(cd_get_bytes(CF_UNICODETEXT, b, sizeof b) > 0);
    EXPECT_WSTR(b, L"second");
    U(CloseClipboard)();
}

TEST(clipboard_dde_hglobal_remains_usable_after_set) {
    CD_SETUP();
    char data[100];
    fill((unsigned char *)data, sizeof data, 3);
    HGLOBAL g = cd_gmem(data, sizeof data);
    SIZE_T before = GlobalSize(g);
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(0x210, g) == g);
    EQ(GlobalSize(g), before);          /* the handle stays valid and sized: the system only borrows it */
    void *p = GlobalLock(g);
    CHECK(p != NULL);
    if (p) { CHECK(!memcmp(p, data, sizeof data)); GlobalUnlock(g); }
    U(CloseClipboard)();
}

TEST(clipboard_dde_hglobal_kept_while_owner_in_process) {
    CD_SETUP();
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    HGLOBAL g = cd_gmem("0123456789abcdef", 16);
    CHECK(cd_open_empty(c->hwnd));
    CHECK(U(SetClipboardData)(0x211, g) == g);
    CHECK(U(CloseClipboard)());
    CHECK(cd_open(c->hwnd));                    /* NtUserOpenClipboard flag == 0 => no ClientEmptyClipboard */
    CHECK(!cd_freed(g));
    EQ(U(GetClipboardData)(0x211), g);
    U(CloseClipboard)();
}

TEST(clipboard_dde_hglobal_freed_by_client_empty_after_owner_cleared) {
    CD_SETUP();
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    HGLOBAL g = cd_gmem("0123456789abcdef", 16);
    CHECK(cd_open_empty(c->hwnd));
    CHECK(U(SetClipboardData)(0x212, g) == g);
    CHECK(U(CloseClipboard)());
    CHECK(cd_open_empty(NULL));                 /* spwndClipOwner becomes NULL */
    /* Windows 10: the client-side free of the HGLOBAL has already happened when EmptyClipboard returns (the
     * handle is invalid before the clipboard is closed or reopened) */
    CHECK(cd_freed(g));
    CHECK(U(CloseClipboard)());
    CHECK(cd_freed(g));
    CHECK(cd_open(c->hwnd));
    CHECK(cd_freed(g));
    U(CloseClipboard)();
}

TEST(clipboard_dde_set_zero_size_hglobal_fails_without_kernel_call) {
    CD_SETUP();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 0);
    if (!g) { t_skip("GlobalAlloc(0)"); return; }
    UINT fmt = cd_reg("zero");
    CHECK(cd_open_empty(NULL));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    CHECK(U(SetClipboardData)(fmt, g) == NULL);   /* ConvertMemHandle: GlobalSize == 0 => NULL, user32 returns before the syscall */
    EQ(U(GetClipboardSequenceNumber)(), s0);
    CHECK(!U(IsClipboardFormatAvailable)(fmt));
    EQ(U(CountClipboardFormats)(), 0);
    U(CloseClipboard)();
    GlobalFree(g);                                /* still ours: nobody took it */
}

TEST(clipboard_dde_set_freed_hglobal_private_format_is_opaque_handle) {
    CD_SETUP();
    HGLOBAL g = cd_gmem("x", 1);
    GlobalFree(g);
    CHECK(cd_open_empty(NULL));
    /* GlobalFlags(freed) == GMEM_INVALID_HANDLE sends user32 down the "not memory" branch and the
     * raw value goes to the kernel as an opaque handle; the outcome is recorded, not asserted. */
    SetLastError(0);
    HANDLE r = U(SetClipboardData)(0x2A0, g);
    t_note("SetClipboardData(private, freed hglobal) -> %p, GetLastError %lu, available %d", r, GetLastError(), U(IsClipboardFormatAvailable)(0x2A0));
    U(EmptyClipboard)();
    U(CloseClipboard)();
}

TEST(clipboard_dde_set_unregistered_atom_format) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    /* InternalSetClipboardData resolves a new entry's name with UserGetAtomName(format & 0xffff) and
     * fails if the atom does not exist */
    SetLastError(0);
    HANDLE r = U(SetClipboardData)(0xFFFF, NULL);
    int avail = U(IsClipboardFormatAvailable)(0xFFFF);
    t_note("SetClipboardData(0xFFFF, NULL) -> %p err %lu available %d", r, GetLastError(), avail);
    U(CloseClipboard)();
}

/* ---- table: every private/registered range round-trips through user32 + kernel ------------------ */
TEST(clipboard_dde_roundtrip_sizes_via_user32) {
    CD_SETUP();
    static const size_t sizes[] = { 1, 2, 3, 7, 8, 9, 63, 64, 65, 4095, 4096, 4097, 65536, 262144 };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        unsigned char *buf = malloc(sizes[i]);
        fill(buf, sizes[i], (unsigned)i);
        HGLOBAL g = cd_gmem(buf, sizes[i]);
        CHECK(cd_open_empty(NULL));
        CHECKF(U(SetClipboardData)(0x230, g) == g, "size %zu", sizes[i]);
        SIZE_T gs = GlobalSize(g);
        void *p = GlobalLock(g);
        CHECKF(p && gs >= sizes[i] && !memcmp(p, buf, sizes[i]), "size %zu", sizes[i]);
        if (p) GlobalUnlock(g);
        U(CloseClipboard)();
        free(buf);
    }
}

/* ---- sequence number ------------------------------------------------------------------------- */
TEST(clipboard_dde_seq_empty_increments_by_one) {
    CD_SETUP();
    CHECK(cd_open(NULL));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    CHECK(U(EmptyClipboard)());                 /* emptying the clipboard bumps the clipboard sequence number */
    EQ(U(GetClipboardSequenceNumber)(), s0 + 1);
    CHECK(U(EmptyClipboard)());
    EQ(U(GetClipboardSequenceNumber)(), s0 + 2);
    CHECK(U(GetClipboardSequenceNumber)() != 0);
    U(CloseClipboard)();
}

TEST(clipboard_dde_seq_each_set_increments_by_one) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    for (UINT i = 0; i < 6; i++) {
        U(SetClipboardData)(0x200 + i, NULL);   /* fIncSerialNumber == 1 from user32 */
        EQ(U(GetClipboardSequenceNumber)(), s0 + i + 1);
    }
    U(SetClipboardData)(0x200, NULL);           /* replacing an existing entry counts as well */
    EQ(U(GetClipboardSequenceNumber)(), s0 + 7);
    U(CloseClipboard)();
}

TEST(clipboard_dde_seq_set_with_data_increments_by_one) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    HGLOBAL g = cd_gmem("12345678", 8);
    CHECK(U(SetClipboardData)(0x220, g) == g);
    EQ(U(GetClipboardSequenceNumber)(), s0 + 1);
    U(CloseClipboard)();
}

TEST(clipboard_dde_seq_reads_do_not_change_it) {
    CD_SETUP();
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(cd_put(c->hwnd, 0x221, "abcdefgh", 8));
    CHECK(cd_open(c->hwnd));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    U(IsClipboardFormatAvailable)(0x221);
    U(CountClipboardFormats)();
    U(EnumClipboardFormats)(0);
    U(GetClipboardData)(0x221);
    U(GetClipboardOwner)();
    U(GetOpenClipboardWindow)();
    UINT lst[2] = { 0x221, CF_TEXT };
    U(GetPriorityClipboardFormat)(lst, 2);
    EQ(U(GetClipboardSequenceNumber)(), s0);
    U(CloseClipboard)();
}

TEST(clipboard_dde_seq_open_close_without_change_is_stable) {
    CD_SETUP();
    CHECK(cd_put(NULL, 0x222, "abcdefgh", 8));      /* the Close inside cd_put clears the changed flag */
    DWORD s0 = U(GetClipboardSequenceNumber)();
    CHECK(cd_open(NULL));
    CHECK(U(CloseClipboard)());
    EQ(U(GetClipboardSequenceNumber)(), s0);
}

TEST(clipboard_dde_seq_synthesized_get_does_not_increment) {
    CD_SETUP();
    CHECK(cd_put_text(NULL, L"synth me"));
    CHECK(cd_open(NULL));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    CHECK(U(GetClipboardData)(CF_TEXT) != NULL);   /* user32 stores the conversion with fIncSerialNumber == 0 */
    CHECK(U(GetClipboardData)(CF_OEMTEXT) != NULL);
    EQ(U(GetClipboardSequenceNumber)(), s0);
    U(CloseClipboard)();
}

TEST(clipboard_dde_seq_close_after_change_not_decreasing) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    HGLOBAL g = cd_gstr_w(L"x");
    U(SetClipboardData)(CF_UNICODETEXT, g);
    DWORD s0 = U(GetClipboardSequenceNumber)();
    CHECK(U(CloseClipboard)());                    /* MungeClipData adds CF_LOCALE/CF_TEXT/CF_OEMTEXT */
    DWORD s1 = U(GetClipboardSequenceNumber)();
    CHECK(s1 >= s0);
    t_note("sequence before close %lu, after close %lu (synthesized entries %lu)", s0, s1, s1 - s0);
}

static DWORD WINAPI thr_seq(LPVOID p) { *(DWORD *)p = U(GetClipboardSequenceNumber)(); return 0; }

TEST(clipboard_dde_seq_shared_between_threads) {
    CD_SETUP();
    DWORD t = 0;
    CHECK(cd_put(NULL, 0x223, "abcdefgh", 8));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    EQ(cd_thread(thr_seq, &t), 0);
    EQ(t, s0);                               /* window-station wide counter, not per thread */
}

TEST(clipboard_dde_seq_direct_matches_user32) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserGetClipboardSequenceNumber_pfn f = UN(NtUserGetClipboardSequenceNumber);
    REQUIRE(f);
    CHECK(cd_put(NULL, 0x224, "abcdefgh", 8));
    EQ(f(), U(GetClipboardSequenceNumber)());
}

TEST(clipboard_dde_seq_changes_across_put_cycles) {
    CD_SETUP();
    CHECK(cd_put(NULL, 0x225, "aaaa", 4));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    CHECK(cd_put(NULL, 0x225, "bbbb", 4));
    DWORD s1 = U(GetClipboardSequenceNumber)();
    CHECK(s1 - s0 >= 2);                            /* Empty + Set per cycle */
    CHECK(s1 != s0);
}

/* ---- format table limits ------------------------------------------------------------------- */
TEST(clipboard_dde_many_private_formats_roundtrip_order) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    for (UINT f = 0x200; f <= 0x2FF; f++) U(SetClipboardData)(f, NULL);   /* growth by UserReAllocPool, one tagCLIP at a time */
    EQ(U(CountClipboardFormats)(), 256);
    EQ(U(GetClipboardSequenceNumber)(), s0 + 256);
    UINT out[300];
    int n = cd_formats(out, 300);
    EQ(n, 256);
    for (int i = 0; i < n && i < 256; i++) EQ(out[i], 0x200u + i);
    U(CloseClipboard)();
}

TEST(clipboard_dde_gdiobj_range_formats_delayed) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    for (UINT f = 0x300; f <= 0x3FF; f++) U(SetClipboardData)(f, NULL);
    EQ(U(CountClipboardFormats)(), 256);
    CHECK(U(IsClipboardFormatAvailable)(0x300));
    CHECK(U(IsClipboardFormatAvailable)(0x3FF));
    CHECK(!U(IsClipboardFormatAvailable)(0x2FF));
    CHECK(!U(IsClipboardFormatAvailable)(0x400));
    CHECK(U(EmptyClipboard)());
    EQ(U(CountClipboardFormats)(), 0);
    U(CloseClipboard)();
}

TEST(clipboard_dde_registered_formats_many_roundtrip) {
    CD_SETUP();
    UINT f[40];
    for (int i = 0; i < 40; i++) {
        char tag[16]; wsprintfA(tag, "many%d", i);
        f[i] = cd_reg(tag);
        CHECK(f[i] >= 0xC000 && f[i] <= 0xFFFF);
    }
    CHECK(cd_open_empty(NULL));
    for (int i = 0; i < 40; i++) {
        HGLOBAL g = cd_gmem(&f[i], sizeof f[i]);
        CHECK(U(SetClipboardData)(f[i], g) == g);
    }
    EQ(U(CountClipboardFormats)(), 40);
    UINT out[64];
    int n = cd_formats(out, 64);
    EQ(n, 40);
    for (int i = 0; i < n && i < 40; i++) EQ(out[i], f[i]);
    for (int i = 0; i < 40; i++) {
        UINT v = 0;
        CHECK(cd_get_bytes(f[i], &v, sizeof v) >= sizeof v);
        EQ(v, f[i]);
    }
    U(CloseClipboard)();
}

/* ---- TYPE_CLIPDATA objects ------------------------------------------------------------------ */
TEST(clipboard_dde_convert_returns_clipdata_handle) {
    CD_SETUP(); CD_NEED_WIN32K();
    REQUIRE(UN(NtUserConvertMemHandle));
    unsigned char buf[100]; fill(buf, sizeof buf, 1);
    HANDLE h = cd_convert(buf, sizeof buf);
    CHECK(h != NULL);
    unsigned char type = 0; unsigned short uniq = 0;
    if (h && cd_clip_type(h, &type, &uniq)) {
        EQ(type, CD_TYPE_CLIPDATA);                        /* HMValidateHandle(h, 6) in NtUserCreateLocalMemHandle */
        EQ(uniq, (unsigned short)((ULONG_PTR)h >> 16));
        CHECK(((ULONG_PTR)h & 0xFFFF) != 0);
    } else t_note("USER handle table entry for %p not readable", h);
    cd_destroy_clipdata(h);
}

TEST(clipboard_dde_convert_distinct_handles) {
    CD_SETUP(); CD_NEED_WIN32K();
    REQUIRE(UN(NtUserConvertMemHandle));
    HANDLE h[8];
    for (int i = 0; i < 8; i++) h[i] = cd_convert("abcdefgh", 8);
    for (int i = 0; i < 8; i++) {
        CHECK(h[i] != NULL);
        for (int j = i + 1; j < 8; j++) CHECK(((ULONG_PTR)h[i] & 0xFFFF) != ((ULONG_PTR)h[j] & 0xFFFF));
    }
    for (int i = 0; i < 8; i++) cd_destroy_clipdata(h[i]);
}

TEST(clipboard_dde_convert_zero_size) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    REQUIRE(UN(NtUserConvertMemHandle) && cl);
    HANDLE h = cd_convert("", 0);                  /* _ConvertMemHandle bumps the allocation to 0x18, size field stays 0 */
    CHECK(h != NULL);
    if (h) {
        DWORD sz = 0x55;
        EQ((ULONG)cl(h, NULL, 0, &sz), (ULONG)CD_NT_BUFFER_TOO_SMALL);
        EQ(sz, 0);
        char c = 0x7E;
        EQ((ULONG)cl(h, &c, 0, &sz), 0);     /* objsize 0 <= buffer size 0: succeeds without copying */
        EQ(c, 0x7E);
    }
    cd_destroy_clipdata(h);
}

TEST(clipboard_dde_createlocal_size_query) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    REQUIRE(UN(NtUserConvertMemHandle) && cl);
    static const DWORD sizes[] = { 1, 7, 100, 4096 };
    for (unsigned i = 0; i < 4; i++) {
        unsigned char *b = malloc(sizes[i]); fill(b, sizes[i], 9);
        HANDLE h = cd_convert(b, sizes[i]);
        DWORD sz = 0xCCCC;
        CHECK(h != NULL);
        if (h) {
            LONG st = cl(h, NULL, 0, &sz);          /* no buffer: STATUS_BUFFER_TOO_SMALL and the size is reported */
            EQ((ULONG)st, (ULONG)CD_NT_BUFFER_TOO_SMALL);
            EQ(sz, sizes[i]);
        }
        cd_destroy_clipdata(h);
        free(b);
    }
}

TEST(clipboard_dde_createlocal_copies_exact_size) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    REQUIRE(UN(NtUserConvertMemHandle) && cl);
    unsigned char src[200], dst[260];
    fill(src, sizeof src, 5);
    memset(dst, 0xA5, sizeof dst);
    HANDLE h = cd_convert(src, sizeof src);
    CHECK(h != NULL);
    if (h) {
        DWORD sz = 0;
        EQ((ULONG)cl(h, dst, sizeof dst, &sz), 0);   /* larger buffer is fine */
        EQ(sz, sizeof src);
        CHECK(!memcmp(dst, src, sizeof src));
        for (size_t i = sizeof src; i < sizeof dst; i++) if (dst[i] != 0xA5) { CHECKF(0, "byte %zu overwritten", i); break; }
        memset(dst, 0x5A, sizeof dst);
        EQ((ULONG)cl(h, dst, sizeof src, &sz), 0);   /* exact size */
        CHECK(!memcmp(dst, src, sizeof src));
        EQ(dst[sizeof src], 0x5A);
        EQ((ULONG)cl(h, dst, sizeof src, NULL), 0);  /* size out pointer is optional */
    }
    cd_destroy_clipdata(h);
}

TEST(clipboard_dde_createlocal_short_buffer_untouched) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    REQUIRE(UN(NtUserConvertMemHandle) && cl);
    unsigned char src[64], dst[64];
    fill(src, sizeof src, 7);
    memset(dst, 0x3C, sizeof dst);
    HANDLE h = cd_convert(src, sizeof src);
    CHECK(h != NULL);
    if (h) {
        DWORD sz = 0;
        EQ((ULONG)cl(h, dst, sizeof src - 1, &sz), (ULONG)CD_NT_BUFFER_TOO_SMALL);
        EQ(sz, sizeof src);                     /* needed size still reported */
        for (size_t i = 0; i < sizeof dst; i++) if (dst[i] != 0x3C) { CHECKF(0, "byte %zu written on failure", i); break; }
    }
    cd_destroy_clipdata(h);
}

TEST(clipboard_dde_createlocal_invalid_handles) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    REQUIRE(UN(NtUserConvertMemHandle) && cl);
    struct cd_ctx *c = cd_new();
    DWORD sz = 0x77;
    static const ULONG_PTR bad[] = { 0, 0x7FFE7FFE, 0x5A5A0003, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        EQ((ULONG)cl((HANDLE)bad[i], NULL, 0, &sz), (ULONG)CD_NT_INVALID_HANDLE);   /* HMValidateHandle(h, 6) fails */
    }
    EQ(sz, 0x77);                               /* nothing written for an invalid handle */
    if (c) EQ((ULONG)cl((HANDLE)c->hwnd, NULL, 0, &sz), (ULONG)CD_NT_INVALID_HANDLE);   /* a window is type 1, not 6 */
    HANDLE h = cd_convert("abcdefgh", 8);
    if (h) {
        unsigned hi = (unsigned)((ULONG_PTR)h >> 16) ^ 0x4000;
        if (hi != 0 && hi != 0xFFFF) {
            HANDLE stale = (HANDLE)(((ULONG_PTR)h & 0xFFFF) | ((ULONG_PTR)hi << 16));
            EQ((ULONG)cl(stale, NULL, 0, &sz), (ULONG)CD_NT_INVALID_HANDLE);       /* uniq mismatch */
        }
    }
    cd_destroy_clipdata(h);
}

TEST(clipboard_dde_convert_roundtrip_size_table) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    REQUIRE(UN(NtUserConvertMemHandle) && cl);
    static const DWORD sizes[] = { 1, 2, 3, 4, 15, 16, 17, 255, 256, 4095, 4096, 65535, 65536, 1 << 20, 4 << 20 };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        unsigned char *src = malloc(sizes[i]), *dst = malloc(sizes[i]);
        fill(src, sizes[i], i);
        HANDLE h = cd_convert(src, sizes[i]);
        CHECKF(h != NULL, "size %lu", sizes[i]);
        if (h) {
            DWORD sz = 0;
            EQ((ULONG)cl(h, dst, sizes[i], &sz), 0);
            EQ(sz, sizes[i]);
            CHECKF(!memcmp(src, dst, sizes[i]), "size %lu", sizes[i]);
        }
        cd_destroy_clipdata(h);
        free(src); free(dst);
    }
}

TEST(clipboard_dde_clipdata_destroyed_by_empty) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserSetClipboardData_pfn st = UN(NtUserSetClipboardData);
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    REQUIRE(UN(NtUserConvertMemHandle) && st && cl);
    UINT fmt = cd_reg("destroy");
    HANDLE h = cd_convert("0123456789", 10);
    CHECK(h != NULL);
    CD_SETCLIPBDATA sp = { 1, 1 };
    CHECK(cd_open_empty(NULL));
    CHECK(st(fmt, h, &sp) != 0);
    DWORD sz = 0;
    EQ((ULONG)cl(h, NULL, 0, &sz), (ULONG)CD_NT_BUFFER_TOO_SMALL);   /* alive while the entry exists */
    unsigned char type = 0;
    if (cd_clip_type(h, &type, NULL)) EQ(type, CD_TYPE_CLIPDATA);
    CHECK(U(EmptyClipboard)());                                              /* the format data objects are destroyed */
    EQ((ULONG)cl(h, NULL, 0, &sz), (ULONG)CD_NT_INVALID_HANDLE);
    if (cd_clip_type(h, &type, NULL)) CHECK(type != CD_TYPE_CLIPDATA);
    U(CloseClipboard)();
}

TEST(clipboard_dde_clipdata_replaced_entry_destroys_old) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserSetClipboardData_pfn st = UN(NtUserSetClipboardData);
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    REQUIRE(UN(NtUserConvertMemHandle) && st && cl);
    UINT fmt = cd_reg("replace");
    HANDLE h1 = cd_convert("11111111", 8), h2 = cd_convert("22222222", 8);
    CD_SETCLIPBDATA sp = { 1, 1 };
    CHECK(h1 && h2);
    CHECK(cd_open_empty(NULL));
    CHECK(st(fmt, h1, &sp) != 0);
    CHECK(st(fmt, h2, &sp) != 0);                      /* InternalSetClipboardData: the old data object is freed, then the new one stored */
    DWORD sz = 0;
    EQ((ULONG)cl(h1, NULL, 0, &sz), (ULONG)CD_NT_INVALID_HANDLE);
    EQ((ULONG)cl(h2, NULL, 0, &sz), (ULONG)CD_NT_BUFFER_TOO_SMALL);
    U(EmptyClipboard)();
    U(CloseClipboard)();
}

TEST(clipboard_dde_clipdata_survives_close_and_reopen) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserSetClipboardData_pfn st = UN(NtUserSetClipboardData);
    NtUserGetClipboardData_pfn gt = UN(NtUserGetClipboardData);
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    REQUIRE(UN(NtUserConvertMemHandle) && st && gt && cl);
    UINT fmt = cd_reg("survive");
    HANDLE h = cd_convert("keepkeep", 8);
    CD_SETCLIPBDATA sp = { 1, 1 };
    CHECK(cd_open_empty(NULL));
    CHECK(st(fmt, h, &sp) != 0);
    CHECK(U(CloseClipboard)());
    CHECK(cd_open(NULL));
    CD_GETCLIPBDATA g = {0};
    EQ(gt(fmt, &g), h);                          /* the same kernel handle comes back */
    EQ(g.uFmtRet, fmt);
    EQ(g.fGlobalHandle, 1);
    char b[8] = {0}; DWORD sz = 0;
    EQ((ULONG)cl(h, b, 8, &sz), 0);
    CHECK(!memcmp(b, "keepkeep", 8));
    U(EmptyClipboard)();
    U(CloseClipboard)();
}

TEST(clipboard_dde_direct_get_after_user32_set_describes_entry) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserGetClipboardData_pfn gt = UN(NtUserGetClipboardData);
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    REQUIRE(gt && cl && UN(NtUserConvertMemHandle));
    UINT fmt = cd_reg("describe");
    unsigned char data[48]; fill(data, sizeof data, 11);
    HGLOBAL g = cd_gmem(data, sizeof data);
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(fmt, g) == g);
    CD_GETCLIPBDATA o = { 0xAAAA, 0xBBBB, (HANDLE)0xCCCC };
    HANDLE k = gt(fmt, &o);
    CHECK(k != NULL);
    EQ(o.uFmtRet, fmt);                           /* format actually returned */
    EQ(o.fGlobalHandle, 1);                       /* tagCLIP+0x10 is the fGlobalHandle user32 passed */
    CHECK(k != (HANDLE)g);                               /* the kernel handle is not the HGLOBAL */
    unsigned char type = 0;
    if (k && cd_clip_type(k, &type, NULL)) EQ(type, CD_TYPE_CLIPDATA);
    unsigned char out[64] = {0}; DWORD sz = 0;
    EQ((ULONG)cl(k, out, sizeof out, &sz), 0);
    EQ(sz, GlobalSize(g));
    CHECK(sz >= sizeof data && !memcmp(out, data, sizeof data));
    EQ(gt(fmt, &o), k);                           /* stable across calls */
    U(CloseClipboard)();
}

TEST(clipboard_dde_kernel_copy_is_snapshot_of_hglobal) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserGetClipboardData_pfn gt = UN(NtUserGetClipboardData);
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    REQUIRE(gt && cl && UN(NtUserConvertMemHandle));
    UINT fmt = cd_reg("snapshot");
    HGLOBAL g = cd_gmem("ORIGINAL", 8);
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(fmt, g) == g);
    char *p = GlobalLock(g);
    if (p) { memcpy(p, "MODIFIED", 8); GlobalUnlock(g); }
    CD_GETCLIPBDATA o = {0};
    HANDLE k = gt(fmt, &o);
    char out[16] = {0}; DWORD sz = 0;
    CHECK(k != NULL);
    if (k) {
        EQ((ULONG)cl(k, out, sizeof out, &sz), 0);
        CHECK(!memcmp(out, "ORIGINAL", 8));              /* _ConvertMemHandle memcpy'd at Set time */
    }
    char cur[8] = {0};
    CHECK(cd_get_bytes(fmt, cur, 8) >= 8);               /* user32 hands back the caller's own (modified) block */
    CHECK(!memcmp(cur, "MODIFIED", 8));
    U(CloseClipboard)();
}

TEST(clipboard_dde_direct_get_missing_format) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserGetClipboardData_pfn gt = UN(NtUserGetClipboardData);
    REQUIRE(gt);
    CHECK(cd_open_empty(NULL));
    CD_GETCLIPBDATA o = { 0x11, 0x22, (HANDLE)0x33 };
    CHECK(gt(0x2B0, &o) == NULL);
    EQ(o.uFmtRet, 0x11);
    EQ(o.fGlobalHandle, 0x22);
    CHECK(gt(0, &o) == NULL);                            /* FindClipFormat(0) never matches */
    CHECK(gt(CF_BITMAP, &o) == NULL);
    U(CloseClipboard)();
}

TEST(clipboard_dde_direct_set_get_roundtrip_flags) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserSetClipboardData_pfn st = UN(NtUserSetClipboardData);
    NtUserGetClipboardData_pfn gt = UN(NtUserGetClipboardData);
    REQUIRE(UN(NtUserConvertMemHandle) && st && gt && UN(NtUserCreateLocalMemHandle));
    UINT f0 = cd_reg("flag0"), f1 = cd_reg("flag1");
    HANDLE h0 = cd_convert("zero----", 8), h1 = cd_convert("one-----", 8);
    CD_SETCLIPBDATA p0 = { 0, 1 }, p1 = { 1, 1 };
    CHECK(cd_open_empty(NULL));
    CHECK(st(f0, h0, &p0) != 0);
    CHECK(st(f1, h1, &p1) != 0);
    CD_GETCLIPBDATA o = {0};
    EQ(gt(f0, &o), h0);
    EQ(o.fGlobalHandle, 0);                       /* stored verbatim in tagCLIP+0x10 */
    EQ(gt(f1, &o), h1);
    EQ(o.fGlobalHandle, 1);
    /* Freeing a generic (type 2) format destroys the TYPE_CLIPDATA object whatever
     * fGlobalHandle says, so both are gone after Empty */
    CHECK(U(EmptyClipboard)());
    DWORD sz = 0;
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    EQ((ULONG)cl(h0, NULL, 0, &sz), (ULONG)CD_NT_INVALID_HANDLE);
    EQ((ULONG)cl(h1, NULL, 0, &sz), (ULONG)CD_NT_INVALID_HANDLE);
    U(CloseClipboard)();
}

TEST(clipboard_dde_direct_set_replace_keeps_single_entry) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserSetClipboardData_pfn st = UN(NtUserSetClipboardData);
    REQUIRE(UN(NtUserConvertMemHandle) && st);
    UINT fmt = cd_reg("single");
    CD_SETCLIPBDATA p = { 1, 1 };
    CHECK(cd_open_empty(NULL));
    for (int i = 0; i < 5; i++) CHECK(st(fmt, cd_convert("abcdefgh", 8), &p) != 0);
    EQ(U(CountClipboardFormats)(), 1);
    U(EmptyClipboard)();
    U(CloseClipboard)();
}

TEST(clipboard_dde_convert_many_handles_released_by_empty) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserSetClipboardData_pfn st = UN(NtUserSetClipboardData);
    NtUserCreateLocalMemHandle_pfn cl = UN(NtUserCreateLocalMemHandle);
    REQUIRE(UN(NtUserConvertMemHandle) && st && cl);
    enum { N = 64 };
    HANDLE h[N]; UINT f[N];
    CD_SETCLIPBDATA p = { 1, 1 };
    CHECK(cd_open_empty(NULL));
    for (int i = 0; i < N; i++) {
        f[i] = 0x240 + i;
        h[i] = cd_convert(&i, sizeof i);
        CHECK(h[i] != NULL);
        CHECK(st(f[i], h[i], &p) != 0);
    }
    EQ(U(CountClipboardFormats)(), N);
    DWORD sz;
    for (int i = 0; i < N; i++) {
        CHECK((ULONG)cl(h[i], NULL, 0, &sz) == (ULONG)CD_NT_BUFFER_TOO_SMALL);
    }
    CHECK(U(EmptyClipboard)());
    int alive = 0;
    for (int i = 0; i < N; i++) if ((ULONG)cl(h[i], NULL, 0, &sz) != (ULONG)CD_NT_INVALID_HANDLE) alive++;
    EQ(alive, 0);
    U(CloseClipboard)();
}
