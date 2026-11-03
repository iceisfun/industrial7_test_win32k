/* RegisterWindowMessage / RegisterClipboardFormat: win32kfull!NtUserRegisterWindowMessage copies the
 * NUL-terminated name (at most 255 characters) into a stack buffer and calls
 * win32kbase!UserAddAtomEx on the session user atom table (RtlAddAtomToAtomTableEx). */
#include "mta.h"

static void ustr(UNICODE_STRING *u, const WCHAR *s, unsigned maxchars) {
    u->Buffer = (PWSTR)s; u->Length = (USHORT)(wcslen(s) * 2); u->MaximumLength = (USHORT)(maxchars * 2);
}
static UINT rwm(const WCHAR *s) { return U(RegisterWindowMessageW)(s); }
static void uname(WCHAR *out, const char *tag) { mta_name(out, 300, tag); }
static void padded(WCHAR *out, const char *tag, unsigned len) {
    uname(out, tag);
    unsigned n = (unsigned)wcslen(out);
    for (unsigned i = n; i < len; i++) out[i] = L'x';
    out[len > n ? len : n] = 0;
}

TEST(msg_timers_atoms_rwm_returns_user_range) {
    MTA_NEED();
    WCHAR n[300]; uname(n, "rwm_range");
    UINT id = rwm(n);
    /* user atoms are RtlAddAtom handles: 0xC000 | slot */
    CHECKF(id >= 0xC000 && id <= 0xFFFF, "id 0x%x", id);
}
TEST(msg_timers_atoms_rwm_same_string_same_id) {
    MTA_NEED();
    WCHAR n[300]; uname(n, "rwm_same");
    UINT a = rwm(n), b = rwm(n), c = rwm(n);
    CHECK(a != 0);
    MEQ(a, b); MEQ(b, c);
}
TEST(msg_timers_atoms_rwm_distinct_strings_distinct_ids) {
    MTA_NEED();
    UINT ids[40];
    for (int i = 0; i < 40; i++) {
        WCHAR n[300], tag[32]; char t[32]; snprintf(t, sizeof t, "rwm_dist_%d", i); (void)tag;
        uname(n, t);
        ids[i] = rwm(n);
        CHECK(ids[i] >= 0xC000 && ids[i] <= 0xFFFF);
    }
    for (int i = 0; i < 40; i++) for (int j = i + 1; j < 40; j++) if (ids[i] == ids[j]) CHECKF(0, "strings %d and %d share id 0x%x", i, j, ids[i]);
}
TEST(msg_timers_atoms_rwm_prefix_strings_are_distinct) {
    MTA_NEED();
    WCHAR a[300], b[300]; uname(a, "rwm_pref"); uname(b, "rwm_prefix");
    CHECK(rwm(a) != rwm(b));
}
TEST(msg_timers_atoms_rwm_case_insensitive) {
    MTA_NEED();
    /* RtlpHashStringToAtom compares names with RtlEqualUnicodeString(..., CaseInSensitive = TRUE) */
    WCHAR lo[300], up[300], mx[300];
    uname(lo, "rwm_case"); wcscpy(up, lo); wcscpy(mx, lo);
    for (WCHAR *p = up; *p; p++) if (*p >= L'a' && *p <= L'z') *p -= 32;
    for (WCHAR *p = mx; *p; p++) if (*p >= L'a' && *p <= L'z' && ((p - mx) & 1)) *p -= 32;
    UINT a = rwm(lo), b = rwm(up), c = rwm(mx);
    CHECK(a != 0);
    MEQ(a, b); MEQ(a, c);
}
TEST(msg_timers_atoms_rwm_case_variants_keep_first_spelling) {
    MTA_NEED();
    WCHAR lo[300], up[300], buf[300];
    uname(lo, "rwm_spell"); wcscpy(up, lo);
    for (WCHAR *p = up; *p; p++) if (*p >= L'a' && *p <= L'z') *p -= 32;
    UINT a = rwm(lo); rwm(up);
    UINT n = U(GetClipboardFormatNameW)(a, buf, 300);
    CHECK(n == wcslen(lo));
    CHECK(!wcscmp(buf, lo));       /* the atom keeps the spelling of the first registration */
}
TEST(msg_timers_atoms_rwm_non_ascii_name) {
    MTA_NEED();
    WCHAR n[300]; uname(n, "rwm_uni");
    size_t l = wcslen(n);
    n[l] = 0x00e9; n[l + 1] = 0x4e2d; n[l + 2] = 0x6587; n[l + 3] = 0;
    UINT a = rwm(n), b = rwm(n);
    CHECK(a >= 0xC000); MEQ(a, b);
    WCHAR up[300]; wcscpy(up, n); up[l] = 0x00c9;
    UINT c = rwm(up);
    if (c != a) t_info("U+00E9 and U+00C9 got different atoms (0x%x, 0x%x)", a, c);
    else t_note("atom names fold non-ASCII case");
}
TEST(msg_timers_atoms_rwm_ansi_matches_wide) {
    MTA_NEED();
    WCHAR w[300]; char a[300];
    uname(w, "rwm_ansi");
    for (int i = 0; ; i++) { a[i] = (char)w[i]; if (!w[i]) break; }
    UINT x = rwm(w), y = U(RegisterWindowMessageA)(a);
    CHECK(x != 0); MEQ(x, y);
}
TEST(msg_timers_atoms_clipboard_format_shares_message_range) {
    MTA_NEED();
    /* user32 maps RegisterClipboardFormatW and RegisterWindowMessageW to one function (same RVA) */
    WCHAR n[300]; uname(n, "rwm_clip");
    UINT a = U(RegisterClipboardFormatW)(n), b = rwm(n);
    CHECK(a >= 0xC000 && a <= 0xFFFF);
    MEQ(a, b);
    char s[300]; for (int i = 0; ; i++) { s[i] = (char)n[i]; if (!n[i]) break; }
    MEQ(U(RegisterClipboardFormatA)(s), a);
}
TEST(msg_timers_atoms_clipboard_format_message_name_roundtrip) {
    MTA_NEED();
    WCHAR n[300], buf[300]; uname(n, "rwm_round");
    UINT id = U(RegisterClipboardFormatW)(n);
    REQUIRE(id);
    UINT len = U(GetClipboardFormatNameW)(id, buf, 300);
    MEQ(len, wcslen(n));
    CHECK(!wcscmp(buf, n));
}

/* ---- name length ------------------------------------------------------------------------------- */
TEST(msg_timers_atoms_rwm_length_table) {
    MTA_NEED();
    /* The copy buffer holds 256 WCHARs including the terminator: 255 characters pass, 256 or more overflow
     * (STATUS_BUFFER_OVERFLOW in the bounded copy) and the call returns 0. */
    static const unsigned ok[] = { 40, 64, 128, 200, 254, 255 };
    static const unsigned bad[] = { 256, 257, 300 };
    WCHAR n[1100];
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        char t[32]; snprintf(t, sizeof t, "len%u_", ok[i]);
        padded(n, t, ok[i]);
        UINT a = rwm(n), b = rwm(n);
        CHECKF(a >= 0xC000 && a == b, "length %u -> 0x%x/0x%x", ok[i], a, b);
    }
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char t[32]; snprintf(t, sizeof t, "len%u_", bad[i]);
        padded(n, t, bad[i]);
        SET_ERR(0);
        UINT a = rwm(n);
        CHECKF(a == 0, "length %u accepted as 0x%x", bad[i], a);
        if (!a) t_note("length %u: GetLastError %lu", bad[i], GetLastError());
    }
}
TEST(msg_timers_atoms_rwm_255_and_256_distinct_prefix) {
    MTA_NEED();
    WCHAR n[400]; padded(n, "len255b_", 255);
    UINT id = rwm(n);
    CHECK(id >= 0xC000);
    WCHAR x[400]; wcscpy(x, n); x[255] = L'y'; x[256] = 0;      /* 256 chars: overflows, does not register a truncation */
    MEQ(rwm(x), 0);
    MEQ(rwm(n), id);                                      /* the 255-char atom is untouched */
}
TEST(msg_timers_atoms_rwm_255_roundtrip_through_name_query) {
    MTA_NEED();
    WCHAR n[400], buf[400]; padded(n, "len255c_", 255);
    UINT id = rwm(n);
    REQUIRE(id);
    MEQ(U(GetClipboardFormatNameW)(id, buf, 400), 255);
    CHECK(!wcscmp(buf, n));
}
TEST(msg_timers_atoms_rwm_empty_string_fails) {
    MTA_NEED();
    SET_ERR(MTA_ERR_SENTINEL);
    MEQ(rwm(L""), 0);      /* RtlAddAtomToAtomTableEx rejects an empty name (STATUS_OBJECT_NAME_INVALID) */
    DWORD e = GetLastError();
    t_note("RegisterWindowMessage(\"\") last error %lu", e);
    SET_ERR(MTA_ERR_SENTINEL);
    MEQ(U(RegisterClipboardFormatW)(L""), 0);
}
TEST(msg_timers_atoms_rwm_space_and_symbol_names) {
    MTA_NEED();
    static const char *tags[] = { "a b", "  lead", "trail  ", "tab\tx", "dot.dot", "{0000-1111}", "#abc", "a#1", "~!@$%", "back\\slash" };
    for (unsigned i = 0; i < sizeof tags / sizeof tags[0]; i++) {
        WCHAR n[300]; uname(n, tags[i]);
        UINT a = rwm(n), b = rwm(n);
        CHECKF(a >= 0xC000 && a == b, "tag \"%s\" -> 0x%x/0x%x", tags[i], a, b);
    }
}
TEST(msg_timers_atoms_rwm_whitespace_only_name) {
    MTA_NEED();
    UINT a = rwm(L" "), b = rwm(L" ");
    CHECK(a >= 0xC000); MEQ(a, b);
    UINT c = rwm(L"  ");
    CHECK(c != a);
}

/* ---- "#nnn" integer atoms ------------------------------------------------------------------------- */
TEST(msg_timers_atoms_rwm_integer_strings) {
    MTA_NEED();
    /* RtlGetIntegerAtom: "#n" below 0xC000 is returned as the atom itself, with no table entry. */
    MEQ(rwm(L"#1"), 1);
    MEQ(rwm(L"#1234"), 1234);
    MEQ(rwm(L"#32770"), 32770);
    MEQ(rwm(L"#49151"), 49151);
}
TEST(msg_timers_atoms_rwm_integer_strings_in_string_range_rejected) {
    MTA_NEED();
    /* an integer atom of 0xC000 or more would alias a string atom: STATUS_INVALID_PARAMETER, result 0 */
    MEQ(rwm(L"#49152"), 0);
    MEQ(rwm(L"#65535"), 0);
}
TEST(msg_timers_atoms_rwm_integer_string_clipboard_alias) {
    MTA_NEED();
    MEQ(U(RegisterClipboardFormatW)(L"#321"), 321);
}
TEST(msg_timers_atoms_rwm_hash_without_digits_is_a_string) {
    MTA_NEED();
    WCHAR n[300]; uname(n, "#nodigits");
    UINT a = rwm(n); CHECK(a >= 0xC000);
    WCHAR m[300]; uname(m, "x#12");
    CHECK(rwm(m) >= 0xC000);
}

/* ---- direct NtUserRegisterWindowMessage ------------------------------------------------------------- */
TEST(msg_timers_atoms_direct_rwm_matches_user32) {
    MTA_NEED();
    NtUserRegisterWindowMessage_pfn f = UN(NtUserRegisterWindowMessage);
    REQUIRE(f);
    WCHAR n[300]; uname(n, "dir_match");
    UNICODE_STRING u; ustr(&u, n, (unsigned)wcslen(n) + 1);
    UINT a = f(&u), b = rwm(n);
    CHECK(a >= 0xC000); MEQ(a, b);
}
TEST(msg_timers_atoms_direct_rwm_max_length_equal_to_length) {
    MTA_NEED();
    NtUserRegisterWindowMessage_pfn f = UN(NtUserRegisterWindowMessage);
    REQUIRE(f);
    WCHAR n[300]; uname(n, "dir_maxeq");
    UNICODE_STRING u; ustr(&u, n, (unsigned)wcslen(n));      /* MaximumLength == Length: probe accepts Length <= MaximumLength */
    UINT a = f(&u);
    CHECK(a >= 0xC000); MEQ(a, rwm(n));
}
TEST(msg_timers_atoms_direct_rwm_stops_at_nul_not_at_length) {
    MTA_NEED();
    NtUserRegisterWindowMessage_pfn f = UN(NtUserRegisterWindowMessage);
    REQUIRE(f);
    /* Length is only used to probe the buffer; the bounded copy runs until the NUL terminator. */
    WCHAR n[300]; uname(n, "dir_nul");
    size_t l = wcslen(n);
    WCHAR m[300]; wcscpy(m, n); m[l] = 0; m[l + 1] = L'z'; m[l + 2] = L'z'; m[l + 3] = 0;
    UNICODE_STRING u; u.Buffer = m; u.Length = (USHORT)((l + 3) * 2); u.MaximumLength = (USHORT)((l + 4) * 2);
    UINT a = f(&u), b = rwm(n);
    CHECK(a >= 0xC000);
    MEQ(a, b);
}
TEST(msg_timers_atoms_direct_rwm_length_shorter_than_string) {
    MTA_NEED();
    NtUserRegisterWindowMessage_pfn f = UN(NtUserRegisterWindowMessage);
    REQUIRE(f);
    WCHAR n[300]; uname(n, "dir_shortlen");
    UNICODE_STRING u; ustr(&u, n, (unsigned)wcslen(n) + 1);
    u.Length = 6;                                            /* claims 3 characters, the copy still takes the whole string */
    UINT a = f(&u);
    CHECK(a >= 0xC000);
    MEQ(a, rwm(n));
}
TEST(msg_timers_atoms_direct_rwm_empty_unicode_string) {
    MTA_NEED();
    NtUserRegisterWindowMessage_pfn f = UN(NtUserRegisterWindowMessage);
    REQUIRE(f);
    static WCHAR e[2] = { 0, 0 };
    UNICODE_STRING u; ustr(&u, e, 1);
    MEQ(f(&u), 0);
}
TEST(msg_timers_atoms_direct_rwm_256_chars_rejected) {
    MTA_NEED();
    NtUserRegisterWindowMessage_pfn f = UN(NtUserRegisterWindowMessage);
    REQUIRE(f);
    WCHAR n[400]; padded(n, "dir_256_", 256);
    UNICODE_STRING u; ustr(&u, n, 257);
    MEQ(f(&u), 0);
    padded(n, "dir_255_", 255);
    ustr(&u, n, 256);
    CHECK(f(&u) >= 0xC000);
}
TEST(msg_timers_atoms_direct_rwm_integer_atom) {
    MTA_NEED();
    NtUserRegisterWindowMessage_pfn f = UN(NtUserRegisterWindowMessage);
    REQUIRE(f);
    WCHAR s[] = L"#4321";
    UNICODE_STRING u; ustr(&u, s, 6);
    MEQ(f(&u), 4321);
}
TEST(msg_timers_atoms_direct_rwm_stub_number) {
    FARPROC p = t_proc_in("win32u.dll", "NtUserRegisterWindowMessage");
    REQUIRE(p);
    CHECK(k_stub_valid(p));
    MEQ(k_stub_number(p), 4151);      /* win32u_syscalls.tsv */
}
TEST(msg_timers_atoms_rwm_many_distinct_names_stay_stable) {
    MTA_NEED();
    UINT ids[60];
    for (int i = 0; i < 60; i++) { WCHAR n[300]; char t[32]; snprintf(t, sizeof t, "stable_%d", i); uname(n, t); ids[i] = rwm(n); }
    for (int i = 0; i < 60; i++) { WCHAR n[300]; char t[32]; snprintf(t, sizeof t, "stable_%d", i); uname(n, t); MEQ(rwm(n), ids[i]); }
}
TEST(msg_timers_atoms_rwm_message_usable_for_window_messages) {
    MTA_NEED();
    HWND w = mta_win(); REQUIRE(w);
    WCHAR n[300]; uname(n, "rwm_post");
    UINT id = rwm(n);
    REQUIRE(id);
    mta_reset();
    /* the registered id is a normal message number outside WM_APP, so the logging window sees nothing for it,
     * but PostMessage must accept it (message numbers 0xC000..0xFFFF are valid) */
    CHECK(U(PostMessageW)(w, id, 1, 2));
    MSG m; int seen = 0;
    for (int i = 0; i < 200 && !seen; i++) { if (U(PeekMessageW)(&m, w, id, id, PM_REMOVE)) seen = 1; else Sleep(2); }
    CHECK(seen);
    if (seen) { MEQ(m.message, id); MEQ(m.wParam, 1); MEQ(m.lParam, 2); }
    mta_destroy(w);
}
TEST(msg_timers_atoms_rwm_message_below_range_is_not_an_atom) {
    MTA_NEED();
    /* ids below 0xC000 returned for "#n" are plain numbers: asking for their name yields "#n", not an error */
    NtUserGetAtomName_pfn g = UN(NtUserGetAtomName);
    REQUIRE(g);
    UINT id = rwm(L"#777");
    MEQ(id, 777);
    WCHAR buf[32]; UNICODE_STRING u; u.Buffer = buf; u.Length = 0; u.MaximumLength = sizeof buf;
    MEQ(g((ATOM)id, &u), 4);
    CHECK(!wcscmp(buf, L"#777"));
}
