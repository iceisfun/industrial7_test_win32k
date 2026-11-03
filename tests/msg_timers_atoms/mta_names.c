/* Reading atom names back: user32!GetClipboardFormatNameW (NtUserGetClipboardFormatName) and the
 * stub-only NtUserGetAtomName, both ending in win32kbase!UserGetAtomName ->
 * RtlQueryAtomInAtomTable(UserAtomTableHandle). */
#include "mta.h"

static UINT rwm(const WCHAR *s) { return U(RegisterWindowMessageW)(s); }
static void uname(WCHAR *out, const char *tag) { mta_name(out, 300, tag); }

/* name query through the stub with a caller-chosen buffer size in characters */
static UINT ga(ATOM a, WCHAR *buf, unsigned cch) {
    NtUserGetAtomName_pfn g = UN(NtUserGetAtomName);
    UNICODE_STRING u; u.Buffer = buf; u.Length = 0; u.MaximumLength = (USHORT)(cch * 2);
    return g ? g(a, &u) : 0;
}
/* find an atom in the user range that is not in use (STATUS_INVALID_HANDLE -> ERROR_INVALID_HANDLE) */
static UINT free_atom(void) {
    WCHAR b[8];
    for (UINT a = 0xFFFF; a >= 0xC000; a--) {
        SET_ERR(0);
        if (!ga((ATOM)a, b, 8) && GetLastError() == ERROR_INVALID_HANDLE) return a;
    }
    return 0;
}

/* ---- GetClipboardFormatNameW buffer protocol ------------------------------------------------------ */
TEST(msg_timers_atoms_cfn_full_name) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "cfn_full");
    UINT id = rwm(n); REQUIRE(id);
    memset(b, 0xAA, sizeof b);
    UINT r = U(GetClipboardFormatNameW)(id, b, 300);
    MEQ(r, wcslen(n));
    CHECK(!wcscmp(b, n));
}
TEST(msg_timers_atoms_cfn_exact_fit_needs_room_for_nul) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "cfn_fit");
    UINT id = rwm(n); REQUIRE(id);
    unsigned len = (unsigned)wcslen(n);
    /* RtlQueryAtomInAtomTable copies the whole name only when the buffer is larger than the name */
    memset(b, 0xAA, sizeof b);
    MEQ(U(GetClipboardFormatNameW)(id, b, len + 1), len);
    CHECK(!wcscmp(b, n));
    memset(b, 0xAA, sizeof b);
    UINT r = U(GetClipboardFormatNameW)(id, b, len);
    MEQ(r, len - 1);
    CHECK(b[len - 1] == 0 && !wcsncmp(b, n, len - 1));
}
TEST(msg_timers_atoms_cfn_truncation_table) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "cfn_trunc");
    UINT id = rwm(n); REQUIRE(id);
    unsigned len = (unsigned)wcslen(n);
    static const unsigned cch[] = { 2, 3, 5, 10 };
    for (unsigned i = 0; i < sizeof cch / sizeof cch[0]; i++) {
        memset(b, 0xAA, sizeof b);
        UINT r = U(GetClipboardFormatNameW)(id, b, cch[i]);
        /* too small: the name is cut to cch-1 characters and terminated, the call still succeeds */
        CHECKF(r == cch[i] - 1, "cch %u -> %u", cch[i], r);
        CHECKF(b[cch[i] - 1] == 0 && !wcsncmp(b, n, cch[i] - 1), "cch %u content", cch[i]);
        CHECK(b[cch[i]] == 0xAAAA);                       /* nothing written beyond the buffer */
    }
    (void)len;
}
TEST(msg_timers_atoms_cfn_tiny_buffers_fail) {
    MTA_NEED();
    WCHAR n[300], b[4]; uname(n, "cfn_tiny");
    UINT id = rwm(n); REQUIRE(id);
    for (unsigned cch = 0; cch <= 1; cch++) {
        b[0] = 0xAAAA; b[1] = 0xAAAA;
        SET_ERR(0);
        MEQ(U(GetClipboardFormatNameW)(id, b, cch), 0);      /* STATUS_BUFFER_TOO_SMALL -> ERROR_INSUFFICIENT_BUFFER */
        EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
        CHECK(b[0] == 0xAAAA);
    }
}
TEST(msg_timers_atoms_cfn_predefined_formats_rejected) {
    MTA_NEED();
    /* NtUserGetClipboardFormatName rejects every format below 0xC000 before touching the atom table */
    static const UINT f[] = { 0, 1, 2, 7, 8, 13, 15, 0x80, 0x200, 0x2FF, 0x300, 0x3FF, 0x8000, 0xBFFF };
    WCHAR b[64];
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) {
        b[0] = 0x5555;
        SET_ERR(0);
        UINT r = U(GetClipboardFormatNameW)(f[i], b, 64);
        CHECKF(r == 0, "format 0x%x returned %u", f[i], r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        CHECK(b[0] == 0x5555);
    }
}
TEST(msg_timers_atoms_cfn_unused_atom_is_invalid_handle) {
    MTA_NEED();
    UINT a = free_atom();
    REQUIRE(a);
    WCHAR b[64];
    SET_ERR(0);
    MEQ(U(GetClipboardFormatNameW)(a, b, 64), 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}
TEST(msg_timers_atoms_cfn_format_argument_is_16_bit) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "cfn_16bit");
    UINT id = rwm(n); REQUIRE(id);
    /* the kernel entry takes the format as a USHORT, so bits 16+ of the UINT are ignored */
    memset(b, 0, sizeof b);
    MEQ(U(GetClipboardFormatNameW)(id | 0x10000, b, 300), wcslen(n));
    CHECK(!wcscmp(b, n));
}
TEST(msg_timers_atoms_cfn_integer_registration_has_no_name) {
    MTA_NEED();
    UINT id = rwm(L"#555");
    MEQ(id, 555);
    WCHAR b[32];
    SET_ERR(0);
    MEQ(U(GetClipboardFormatNameW)(id, b, 32), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(msg_timers_atoms_cfn_ansi_variant) {
    MTA_NEED();
    WCHAR n[300]; uname(n, "cfn_ansi");
    char a[300], b[300];
    for (int i = 0; ; i++) { a[i] = (char)n[i]; if (!n[i]) break; }
    UINT id = U(RegisterClipboardFormatA)(a); REQUIRE(id);
    memset(b, 0, sizeof b);
    MEQ(U(GetClipboardFormatNameA)(id, b, 300), strlen(a));
    EXPECT_STR(b, a);
}
TEST(msg_timers_atoms_cfn_direct_stub_matches_user32) {
    MTA_NEED();
    NtUserGetClipboardFormatName_pfn f = UN(NtUserGetClipboardFormatName);
    REQUIRE(f);
    WCHAR n[300], b[300]; uname(n, "cfn_direct");
    UINT id = rwm(n); REQUIRE(id);
    MEQ(f(id, b, 300), wcslen(n));
    CHECK(!wcscmp(b, n));
    SET_ERR(0);
    MEQ(f(1, b, 300), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    MEQ(f(0xBFFF, b, 300), 0);
}
TEST(msg_timers_atoms_cfn_zero_length_buffer_with_valid_pointer) {
    MTA_NEED();
    NtUserGetClipboardFormatName_pfn f = UN(NtUserGetClipboardFormatName);
    REQUIRE(f);
    WCHAR b[2] = { 0x7777, 0x7777 };
    SET_ERR(0);
    MEQ(f(0x10, b, 0), 0);             /* the format check precedes any atom lookup */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(b[0] == 0x7777);
}
TEST(msg_timers_atoms_cfn_stub_number) {
    FARPROC p = t_proc_in("win32u.dll", "NtUserGetClipboardFormatName");
    REQUIRE(p);
    CHECK(k_stub_valid(p));
    MEQ(k_stub_number(p), 4316);
}

/* ---- NtUserGetAtomName ---------------------------------------------------------------------------- */
TEST(msg_timers_atoms_getatomname_string_atom) {
    MTA_NEED();
    NtUserGetAtomName_pfn g = UN(NtUserGetAtomName); REQUIRE(g);
    WCHAR n[300], b[300]; uname(n, "gan_str");
    UINT id = rwm(n); REQUIRE(id);
    UNICODE_STRING u; u.Buffer = b; u.Length = 0x1234; u.MaximumLength = sizeof b;
    memset(b, 0, sizeof b);
    MEQ(g((ATOM)id, &u), wcslen(n));
    CHECK(!wcscmp(b, n));
    MEQ(u.Length, 0x1234);           /* the wrapper only fills the buffer; the structure is not updated */
}
TEST(msg_timers_atoms_getatomname_truncation_table) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "gan_trunc");
    UINT id = rwm(n); REQUIRE(id);
    unsigned len = (unsigned)wcslen(n);
    static const unsigned cch[] = { 2, 4, 7, 20 };
    for (unsigned i = 0; i < sizeof cch / sizeof cch[0]; i++) {
        memset(b, 0xAA, sizeof b);
        UINT r = ga((ATOM)id, b, cch[i]);
        CHECKF(r == cch[i] - 1, "cch %u -> %u", cch[i], r);
        CHECKF(b[cch[i] - 1] == 0 && !wcsncmp(b, n, cch[i] - 1), "cch %u content", cch[i]);
        CHECK(b[cch[i]] == 0xAAAA);
    }
    memset(b, 0xAA, sizeof b);
    MEQ(ga((ATOM)id, b, len + 1), len);
    memset(b, 0xAA, sizeof b);
    MEQ(ga((ATOM)id, b, len), len - 1);
}
TEST(msg_timers_atoms_getatomname_tiny_buffers_fail) {
    MTA_NEED();
    WCHAR n[300], b[4]; uname(n, "gan_tiny");
    UINT id = rwm(n); REQUIRE(id);
    for (unsigned cch = 0; cch <= 1; cch++) {
        b[0] = 0xAAAA;
        SET_ERR(0);
        MEQ(ga((ATOM)id, b, cch), 0);
        EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
        CHECK(b[0] == 0xAAAA);
    }
}
TEST(msg_timers_atoms_getatomname_integer_atoms_render_as_hash) {
    MTA_NEED();
    /* atoms below 0xC000 are not table entries: RtlQueryAtomInAtomTable formats them as "#%u" */
    static const struct { ATOM a; const WCHAR *s; } v[] = {
        { 1, L"#1" }, { 9, L"#9" }, { 10, L"#10" }, { 255, L"#255" }, { 0x8000, L"#32768" },
        { 0x8001, L"#32769" }, { 0x8002, L"#32770" }, { 0x8003, L"#32771" }, { 0xBFFF, L"#49151" },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        WCHAR b[32]; memset(b, 0, sizeof b);
        UINT r = ga(v[i].a, b, 32);
        CHECKF(r == wcslen(v[i].s) && !wcscmp(b, v[i].s), "atom 0x%x -> %u \"%ls\"", v[i].a, r, b);
    }
}
TEST(msg_timers_atoms_getatomname_integer_atom_truncation) {
    MTA_NEED();
    WCHAR b[16];
    memset(b, 0xAA, sizeof b);
    MEQ(ga(0x8002, b, 3), 2);          /* "#32770" cut to "#3" */
    CHECK(!wcscmp(b, L"#3"));
    SET_ERR(0);
    MEQ(ga(0x8002, b, 1), 0);
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    SET_ERR(0);
    MEQ(ga(0x8002, b, 0), 0);
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
}
TEST(msg_timers_atoms_getatomname_atom_zero_invalid_parameter) {
    MTA_NEED();
    WCHAR b[16];
    SET_ERR(0);
    MEQ(ga(0, b, 16), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(msg_timers_atoms_getatomname_unused_atom_invalid_handle) {
    MTA_NEED();
    UINT a = free_atom(); REQUIRE(a);
    WCHAR b[16];
    SET_ERR(0);
    MEQ(ga((ATOM)a, b, 16), 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}
TEST(msg_timers_atoms_getatomname_matches_clipboard_name) {
    MTA_NEED();
    WCHAR n[300], b1[300], b2[300]; uname(n, "gan_same");
    UINT id = rwm(n); REQUIRE(id);
    UINT r1 = ga((ATOM)id, b1, 300), r2 = U(GetClipboardFormatNameW)(id, b2, 300);
    MEQ(r1, r2);
    CHECK(!wcscmp(b1, b2));
}
TEST(msg_timers_atoms_getatomname_stub_number) {
    FARPROC p = t_proc_in("win32u.dll", "NtUserGetAtomName");
    REQUIRE(p);
    CHECK(k_stub_valid(p));
    MEQ(k_stub_number(p), 4258);
}
TEST(msg_timers_atoms_getatomname_ignores_atom_bits_above_16) {
    MTA_NEED();
    NtUserGetAtomName_pfn g = UN(NtUserGetAtomName); REQUIRE(g);
    WCHAR n[300], b[300]; uname(n, "gan_bits");
    UINT id = rwm(n); REQUIRE(id);
    UNICODE_STRING u; u.Buffer = b; u.Length = 0; u.MaximumLength = sizeof b;
    /* the kernel declares the atom as a 16-bit value (cx), so a dirty upper half in rcx is not part of it */
    typedef UINT (WINAPI *wide_pfn)(ULONG_PTR, UNICODE_STRING *);
    memset(b, 0, sizeof b);
    UINT r = ((wide_pfn)g)((ULONG_PTR)id | 0xABCD0000ULL, &u);
    MEQ(r, wcslen(n));
}

/* ---- kernel32 global atom table is separate --------------------------------------------------------- */
TEST(msg_timers_atoms_user_atom_not_in_global_table) {
    MTA_NEED();
    WCHAR n[300]; uname(n, "sep_user");
    UINT u = rwm(n); REQUIRE(u);
    SET_ERR(0);
    MEQ(GlobalFindAtomW(n), 0);        /* RegisterWindowMessage adds to the user table only */
    EXPECT_ERR(ERROR_FILE_NOT_FOUND);
}
TEST(msg_timers_atoms_global_atom_not_in_user_table) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "sep_glob");
    ATOM g = GlobalAddAtomW(n);
    REQUIRE(g);
    memset(b, 0, sizeof b);
    UINT r = U(GetClipboardFormatNameW)(g, b, 300);
    /* the same numeric atom in the user table, if it exists at all, names a different string */
    CHECK(!(r == wcslen(n) && !wcscmp(b, n)));
    UINT u = rwm(n);
    CHECK(u >= 0xC000);
    t_info("global atom 0x%x vs user atom 0x%x for the same string", g, u);
    GlobalDeleteAtom(g);
}
TEST(msg_timers_atoms_global_delete_does_not_touch_user_atom) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "sep_del");
    ATOM g = GlobalAddAtomW(n); REQUIRE(g);
    UINT u1 = rwm(n);
    GlobalDeleteAtom(g);
    SET_ERR(0);
    MEQ(GlobalFindAtomW(n), 0);
    UINT u2 = rwm(n);
    MEQ(u1, u2);
    MEQ(U(GetClipboardFormatNameW)(u1, b, 300), wcslen(n));
}
TEST(msg_timers_atoms_user_atom_does_not_touch_global_atom) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "sep_keep");
    ATOM g = GlobalAddAtomW(n); REQUIRE(g);
    rwm(n);
    MEQ(GlobalFindAtomW(n), g);
    UINT l = GlobalGetAtomNameW(g, b, 300);
    MEQ(l, wcslen(n));
    GlobalDeleteAtom(g);
}
TEST(msg_timers_atoms_global_atoms_are_refcounted_user_registrations_are_not_visible) {
    MTA_NEED();
    WCHAR n[300]; uname(n, "sep_ref");
    ATOM g1 = GlobalAddAtomW(n), g2 = GlobalAddAtomW(n);
    REQUIRE(g1);
    MEQ(g1, g2);
    CHECK(GlobalDeleteAtom(g1) == 0);       /* returns 0 on success */
    MEQ(GlobalFindAtomW(n), g1);      /* the second add holds a reference */
    CHECK(GlobalDeleteAtom(g1) == 0);
    SET_ERR(0);
    MEQ(GlobalFindAtomW(n), 0);
}
TEST(msg_timers_atoms_global_integer_atom_and_rwm_integer_alias) {
    MTA_NEED();
    MEQ(GlobalAddAtomW(L"#2468"), 2468);
    MEQ(rwm(L"#2468"), 2468);
    GlobalDeleteAtom(2468);
}
