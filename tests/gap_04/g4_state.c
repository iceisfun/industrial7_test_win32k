/* Per-window feedback settings and shutdown-block reasons: both are kept as internal window properties
 * (gatomFeedbackSettings / gatomShutdownBlockingReason) set and read only through these syscalls. */
#include "gap4.h"

static const ULONG_PTR k_bad[] = { 0, 0xFFFF, 0x1FFF0, 0x7FFFFFFF, 0xFFFFFFFF };
#define NBAD (sizeof k_bad / sizeof k_bad[0])
#define BAD(i) ((HWND)k_bad[i])

static BOOL fb_set(HWND h, int t, DWORD flags, UINT sz, DWORD *cfg) { return UN(NtUserSetWindowFeedbackSetting)(h, t, flags, sz, cfg); }
static BOOL fb_get(HWND h, int t, DWORD flags, UINT *sz, DWORD *cfg) { return UN(NtUserGetWindowFeedbackSetting)(h, t, flags, sz, cfg); }
static BOOL fb_setv(HWND h, int t, DWORD v) { return fb_set(h, t, 0, 4, &v); }

/* ---- NtUserSetWindowFeedbackSetting / GetWindowFeedbackSetting ---------------------------------- */
TEST(gap4_feedback_invalid_hwnd_fails_1400) {
    G4REQ(NtUserSetWindowFeedbackSetting); G4REQ(NtUserGetWindowFeedbackSetting);
    for (unsigned i = 0; i < NBAD; i++) {
        DWORD v = 1; UINT sz = 4;
        SET_ERR(0);
        CHECK(fb_set(BAD(i), 1, 0, 4, &v) == 0); EXPECT_ERR(G4_ERR_INVALID_WINDOW);
        SET_ERR(0);
        CHECK(fb_get(BAD(i), 1, 0, &sz, &v) == 0); EXPECT_ERR(G4_ERR_INVALID_WINDOW);
    }
}
/* Feedback types are 1..13: 0 and positive values above 13 fail with 0x57 in both directions. Negative values are not
 * range checked the same way (see gap4_feedback_negative_types_are_not_rejected). */
TEST(gap4_feedback_type_range_is_1_to_13) {
    G4REQ(NtUserSetWindowFeedbackSetting); G4REQ(NtUserGetWindowFeedbackSetting);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    static const int bad[] = { 0, 14, 15, 16, 32, 33, 100, 0x10001, 0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        DWORD v = 1; UINT sz = 4;
        SET_ERR(0);
        CHECKF(fb_set(h, bad[i], 0, 4, &v) == 0, "set type %d", bad[i]); EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SET_ERR(0);
        CHECKF(fb_get(h, bad[i], 0, &sz, &v) == 0, "get type %d", bad[i]); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    g4_destroy(h);
}
/* Observed on Windows 10 22H2: a negative type is accepted by Set (TRUE, no error) instead of failing with 0x57. */
TEST(gap4_feedback_negative_types_are_not_rejected) {
    G4REQ(NtUserSetWindowFeedbackSetting);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    static const int neg[] = { -1, -2, -13, -14, -16, (int)0x80000000, (int)0x80000001 };
    for (unsigned i = 0; i < sizeof neg / sizeof neg[0]; i++) {
        DWORD v = 1;
        SET_ERR(0xBEEF);
        CHECKF(fb_set(h, neg[i], 0, 4, &v) == 1, "set type %d", neg[i]);
        EXPECT_ERR(0xBEEF);
        fb_set(h, neg[i], 0, 0, NULL);                 /* drop whatever it stored */
    }
    g4_destroy(h);
}
TEST(gap4_feedback_set_get_roundtrip_all_types) {
    G4REQ(NtUserSetWindowFeedbackSetting); G4REQ(NtUserGetWindowFeedbackSetting);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    for (int t = 1; t <= 13; t++) {
        DWORD out = 0xcc; UINT sz = 4;
        CHECKF(fb_setv(h, t, 1) == 1, "set TRUE type %d", t);
        CHECKF(fb_get(h, t, 0, &sz, &out) == 1 && out == 1 && sz == 4, "get after TRUE type %d out %lu sz %u", t, out, sz);
        CHECKF(fb_setv(h, t, 0) == 1, "set FALSE type %d", t);
        out = 0xcc; sz = 4;
        CHECKF(fb_get(h, t, 0, &sz, &out) == 1 && out == 0, "get after FALSE type %d out %lu", t, out);
    }
    g4_destroy(h);
}
/* FeedbackSetWindowSetting stores one bit; any non-zero configuration value reads back as TRUE (1). */
TEST(gap4_feedback_nonzero_value_reads_back_as_true) {
    G4REQ(NtUserSetWindowFeedbackSetting); G4REQ(NtUserGetWindowFeedbackSetting);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    static const DWORD vals[] = { 2, 5, 0x80000000, 0xffffffff, 0x100 };
    for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        DWORD out = 0xcc; UINT sz = 4;
        CHECK(fb_setv(h, 3, vals[i]) == 1);
        CHECK(fb_get(h, 3, 0, &sz, &out) == 1); CHECKF(out == 1, "value 0x%lx read back as %lu", vals[i], out);
    }
    g4_destroy(h);
}
/* Types are independent: setting one leaves the others unset (Get returns FALSE). */
TEST(gap4_feedback_types_are_independent) {
    G4REQ(NtUserSetWindowFeedbackSetting); G4REQ(NtUserGetWindowFeedbackSetting);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    CHECK(fb_setv(h, 5, 1) == 1);
    for (int t = 1; t <= 13; t++) {
        DWORD out = 0xcc; UINT sz = 4;
        BOOL r = fb_get(h, t, 0, &sz, &out);
        if (t == 5) CHECK(r == 1 && out == 1); else CHECKF(r == 0 && out == 0, "type %d ret %d out %lu", t, r, out);
    }
    g4_destroy(h);
}
/* A never-set window: FeedbackGetWindowSetting fails but NtUser writes the zeroed local out value to *config. */
TEST(gap4_feedback_get_unset_returns_false_and_zeroes_config) {
    G4REQ(NtUserGetWindowFeedbackSetting);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    DWORD out = 0xcc; UINT sz = 4;
    SET_ERR(0);
    CHECK(fb_get(h, 1, 0, &sz, &out) == 0);
    CHECK(out == 0);
    EXPECT_ERR(0);
    g4_destroy(h);
}
/* Size 0 and a NULL configuration clears the setting (FeedbackClearWindowSetting), which also succeeds when nothing was set. */
TEST(gap4_feedback_clear_removes_setting) {
    G4REQ(NtUserSetWindowFeedbackSetting); G4REQ(NtUserGetWindowFeedbackSetting);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    DWORD out = 0xcc; UINT sz = 4;
    CHECK(fb_set(h, 7, 0, 0, NULL) == 1);              /* nothing set yet */
    CHECK(fb_setv(h, 7, 1) == 1);
    CHECK(fb_get(h, 7, 0, &sz, &out) == 1 && out == 1);
    CHECK(fb_set(h, 7, 0, 0, NULL) == 1);
    out = 0xcc; sz = 4;
    CHECK(fb_get(h, 7, 0, &sz, &out) == 0 && out == 0);
    CHECK(fb_setv(h, 7, 0) == 1);                      /* explicit FALSE is different from cleared */
    CHECK(fb_get(h, 7, 0, &sz, &out) == 1 && out == 0);
    g4_destroy(h);
}
/* The configuration is read whenever the pointer is non-NULL, whatever the size: size 0 with a pointer stores the value
 * (only a NULL pointer takes the FeedbackClearWindowSetting branch). */
TEST(gap4_feedback_size0_with_config_pointer_sets) {
    G4REQ(NtUserSetWindowFeedbackSetting); G4REQ(NtUserGetWindowFeedbackSetting);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    DWORD v = 1, out = 0xcc; UINT sz = 4;
    CHECK(fb_set(h, 2, 0, 0, &v) == 1);
    CHECK(fb_get(h, 2, 0, &sz, &out) == 1 && out == 1);
    g4_destroy(h);
}
/* Invalid sizes and flags: size must be 0 or 4, flags must be 0, a non-zero size needs a config. */
TEST(gap4_feedback_set_rejects_bad_size_flags_null_config) {
    G4REQ(NtUserSetWindowFeedbackSetting);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    DWORD v = 1;
    static const UINT sizes[] = { 1, 2, 3, 5, 8, 12, 0x14, 0xffffffff };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        SET_ERR(0); CHECKF(fb_set(h, 1, 0, sizes[i], &v) == 0, "size %u", sizes[i]); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    static const DWORD flags[] = { 1, 2, 4, 0x80, 0x10000, 0x80000000 };
    for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        SET_ERR(0); CHECKF(fb_set(h, 1, flags[i], 4, &v) == 0, "flags 0x%lx", flags[i]); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    SET_ERR(0); CHECK(fb_set(h, 1, 0, 4, NULL) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    g4_destroy(h);
}
/* A failed Set must not create a setting. */
TEST(gap4_feedback_failed_set_leaves_window_unchanged) {
    G4REQ(NtUserSetWindowFeedbackSetting); G4REQ(NtUserGetWindowFeedbackSetting);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    DWORD v = 1, out = 0xcc; UINT sz = 4;
    CHECK(fb_set(h, 4, 1, 4, &v) == 0);
    CHECK(fb_set(h, 4, 0, 3, &v) == 0);
    CHECK(fb_get(h, 4, 0, &sz, &out) == 0);
    g4_destroy(h);
}
/* Get: pSize is mandatory, flags may only be 0 or 1. */
TEST(gap4_feedback_get_rejects_null_size_and_bad_flags) {
    G4REQ(NtUserGetWindowFeedbackSetting);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    DWORD out = 0; UINT sz = 4;
    SET_ERR(0); CHECK(fb_get(h, 1, 0, NULL, &out) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    static const DWORD flags[] = { 2, 3, 4, 0x80, 0x80000000, 0xfffffffe };
    for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        SET_ERR(0); CHECKF(fb_get(h, 1, flags[i], &sz, &out) == 0, "flags 0x%lx", flags[i]); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    g4_destroy(h);
}
/* *pSize != 4 is overwritten with 4. With a configuration pointer and *pSize < 4 the call fails with
 * ERROR_INSUFFICIENT_BUFFER (0x7a) after that write; without a config pointer, or with *pSize > 3, it proceeds. */
TEST(gap4_feedback_get_size_protocol) {
    G4REQ(NtUserSetWindowFeedbackSetting); G4REQ(NtUserGetWindowFeedbackSetting);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    CHECK(fb_setv(h, 6, 1) == 1);
    for (UINT s0 = 0; s0 < 4; s0++) {
        DWORD out = 0xcc; UINT sz = s0;
        SET_ERR(0);
        CHECKF(fb_get(h, 6, 0, &sz, &out) == 0, "size %u", s0);
        EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
        CHECKF(sz == 4, "size %u written back as %u", s0, sz);
        CHECK(out == 0xcc);
    }
    for (UINT s0 = 4; s0 < 12; s0 += 3) {
        DWORD out = 0xcc; UINT sz = s0;
        CHECKF(fb_get(h, 6, 0, &sz, &out) == 1 && out == 1, "size %u", s0);
        CHECKF(sz == 4, "size %u written back as %u", s0, sz);
    }
    { UINT sz = 2; CHECK(fb_get(h, 6, 0, &sz, NULL) == 1); CHECK(sz == 4); }   /* no config: nothing to overflow */
    { UINT sz = 0; DWORD out = 0xcc; (void)out; CHECK(fb_get(h, 6, 0, &sz, NULL) == 1); CHECK(sz == 4); }
    g4_destroy(h);
}
/* flags & 1 walks up to the parent of a non-top-level window (FeedbackGetWindowSetting loop). */
TEST(gap4_feedback_get_flag1_inherits_from_parent) {
    G4REQ(NtUserSetWindowFeedbackSetting); G4REQ(NtUserGetWindowFeedbackSetting);
    HWND p = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    HWND c = p ? g4_window(WS_CHILD, 0, 0, 0, 40, 40, p) : NULL;
    if (!c) { t_skip("no windows"); g4_destroy(p); return; }
    DWORD out = 0xcc; UINT sz = 4;
    CHECK(fb_setv(p, 9, 1) == 1);
    CHECK(fb_get(c, 9, 0, &sz, &out) == 0);
    out = 0xcc; sz = 4;
    CHECK(fb_get(c, 9, 1, &sz, &out) == 1 && out == 1);
    CHECK(fb_setv(c, 9, 0) == 1);                           /* the child's own setting wins */
    out = 0xcc; sz = 4;
    CHECK(fb_get(c, 9, 1, &sz, &out) == 1 && out == 0);
    /* a top-level window never inherits from its owner/desktop */
    HWND t2 = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (t2) { out = 0xcc; sz = 4; CHECK(fb_get(t2, 9, 1, &sz, &out) == 0); g4_destroy(t2); }
    g4_destroy(p);
}
TEST(gap4_feedback_settings_are_per_window) {
    G4REQ(NtUserSetWindowFeedbackSetting); G4REQ(NtUserGetWindowFeedbackSetting);
    HWND a = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    HWND b = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!a || !b) { t_skip("no windows"); g4_destroy(a); g4_destroy(b); return; }
    DWORD out = 0xcc; UINT sz = 4;
    CHECK(fb_setv(a, 8, 1) == 1);
    CHECK(fb_get(b, 8, 0, &sz, &out) == 0);
    CHECK(fb_setv(b, 8, 0) == 1);
    out = 0xcc; sz = 4; CHECK(fb_get(a, 8, 0, &sz, &out) == 1 && out == 1);
    g4_destroy(a); g4_destroy(b);
}
/* NtUserSet...: the window must belong to the calling process. The failure leaves the last error at 0 because
 * the 5 (access denied) is overwritten by the trailing UserSetLastError. */
TEST(gap4_feedback_set_foreign_window_denied) {
    G4REQ(NtUserSetWindowFeedbackSetting); REQUIRE(U(GetShellWindow)); REQUIRE(U(GetDesktopWindow));
    HWND wins[2] = { U(GetShellWindow)(), U(GetDesktopWindow)() };
    for (int i = 0; i < 2; i++) {
        if (!wins[i]) { t_note("no window %d", i); continue; }
        DWORD v = 1; SET_ERR(0xdead);
        BOOL r = fb_set(wins[i], 1, 0, 4, &v);
        CHECKF(r == 0, "window %p ret %d", wins[i], r);
        DWORD e = GetLastError();
        t_note("foreign window set: gle=%lu", e);
        CHECK(e == 0 || e == 5);
    }
}
TEST(gap4_feedback_get_foreign_window_readable) {
    G4REQ(NtUserGetWindowFeedbackSetting); REQUIRE(U(GetDesktopWindow));
    DWORD out = 0xcc; UINT sz = 4;
    BOOL r = fb_get(U(GetDesktopWindow)(), 1, 0, &sz, &out);   /* Get has no ownership check */
    t_note("desktop window feedback type 1: ret=%d out=%lu", r, out);
    CHECK(sz == 4);
}

/* ---- ShutdownBlockReasonCreate / Query / Destroy ------------------------------------------------ */
static int sb_create(HWND h, const WCHAR *s) { return UN(NtUserShutdownBlockReasonCreate)(h, s, (UINT)lstrlenW(s)); }
static BOOL sb_query(HWND h, WCHAR *buf, DWORD *cch) { return UN(NtUserShutdownBlockReasonQuery)(h, buf, cch); }

TEST(gap4_shutdown_reason_invalid_hwnd_fails_1400) {
    G4REQ(NtUserShutdownBlockReasonCreate); G4REQ(NtUserShutdownBlockReasonQuery); G4REQ(NtUserShutdownReasonDestroy);
    for (unsigned i = 0; i < NBAD; i++) {
        WCHAR buf[8]; DWORD n = 8;
        SET_ERR(0); CHECK(sb_create(BAD(i), L"x") == 0); EXPECT_ERR(G4_ERR_INVALID_WINDOW);
        SET_ERR(0); CHECK(sb_query(BAD(i), buf, &n) == 0); EXPECT_ERR(G4_ERR_INVALID_WINDOW);
        SET_ERR(0); CHECK(UN(NtUserShutdownReasonDestroy)(BAD(i)) == 0); EXPECT_ERR(G4_ERR_INVALID_WINDOW);
    }
}
TEST(gap4_shutdown_reason_create_query_roundtrip) {
    G4REQ(NtUserShutdownBlockReasonCreate); G4REQ(NtUserShutdownBlockReasonQuery); G4REQ(NtUserShutdownReasonDestroy);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    CHECK(sb_create(h, L"win32kt reason") == 1);
    DWORD n = 0;
    CHECK(sb_query(h, NULL, &n) == 1);
    CHECKF(n == 15, "required length %lu (14 chars + NUL)", n);
    WCHAR buf[32]; memset(buf, 0xee, sizeof buf); n = 32;
    CHECK(sb_query(h, buf, &n) == 1);
    EXPECT_WSTR(buf, L"win32kt reason");
    CHECK(n == 15);
    CHECK(UN(NtUserShutdownReasonDestroy)(h) == 1);
    g4_destroy(h);
}
/* The copy is RtlStringCchCopyW into min(*pcch, 0x101) characters (truncating, always terminated); *pcch reports the full length + 1. */
TEST(gap4_shutdown_reason_query_truncates_and_reports_full_length) {
    G4REQ(NtUserShutdownBlockReasonCreate); G4REQ(NtUserShutdownBlockReasonQuery);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    CHECK(sb_create(h, L"abcdefgh") == 1);
    WCHAR buf[16]; DWORD n = 4;
    memset(buf, 0xee, sizeof buf);
    CHECK(sb_query(h, buf, &n) == 1);
    EXPECT_WSTR(buf, L"abc");
    CHECK(n == 9);
    CHECK(buf[4] == 0xeeee);                   /* nothing written past the supplied capacity */
    n = 9; memset(buf, 0xee, sizeof buf);
    CHECK(sb_query(h, buf, &n) == 1); EXPECT_WSTR(buf, L"abcdefgh"); CHECK(n == 9);
    n = 1; memset(buf, 0xee, sizeof buf);
    CHECK(sb_query(h, buf, &n) == 1); CHECK(buf[0] == 0); CHECK(n == 9);
    g4_destroy(h);
}
TEST(gap4_shutdown_reason_query_zero_capacity_with_buffer_fails_87) {
    G4REQ(NtUserShutdownBlockReasonCreate); G4REQ(NtUserShutdownBlockReasonQuery);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    CHECK(sb_create(h, L"abc") == 1);
    WCHAR buf[4] = { 0x1234 }; DWORD n = 0;
    SET_ERR(0);
    CHECK(sb_query(h, buf, &n) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(buf[0] == 0x1234);
    g4_destroy(h);
}
TEST(gap4_shutdown_reason_query_without_reason_fails_87) {
    G4REQ(NtUserShutdownBlockReasonQuery);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    WCHAR buf[8]; DWORD n = 8;
    SET_ERR(0); CHECK(sb_query(h, buf, &n) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    n = 0; SET_ERR(0); CHECK(sb_query(h, NULL, &n) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    g4_destroy(h);
}
/* A second Create replaces the first (InternalRemoveProp then InternalSetProp). */
TEST(gap4_shutdown_reason_create_replaces_previous) {
    G4REQ(NtUserShutdownBlockReasonCreate); G4REQ(NtUserShutdownBlockReasonQuery);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    CHECK(sb_create(h, L"first reason that is long") == 1);
    CHECK(sb_create(h, L"second") == 1);
    WCHAR buf[64]; DWORD n = 64;
    CHECK(sb_query(h, buf, &n) == 1); EXPECT_WSTR(buf, L"second"); CHECK(n == 7);
    g4_destroy(h);
}
/* Length 0 stores an empty string; Query then reports 1 character (the NUL). */
TEST(gap4_shutdown_reason_empty_string) {
    G4REQ(NtUserShutdownBlockReasonCreate); G4REQ(NtUserShutdownBlockReasonQuery);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    CHECK(UN(NtUserShutdownBlockReasonCreate)(h, NULL, 0) == 1);
    DWORD n = 0; CHECK(sb_query(h, NULL, &n) == 1); CHECK(n == 1);
    WCHAR buf[4] = { 0x1234 }; n = 4;
    CHECK(sb_query(h, buf, &n) == 1); CHECK(buf[0] == 0); CHECK(n == 1);
    g4_destroy(h);
}
/* Kernel limit: argument 3 < 0x102 characters; 0x101 is accepted, 0x102 fails with 0x57. */
TEST(gap4_shutdown_reason_kernel_length_limit) {
    G4REQ(NtUserShutdownBlockReasonCreate); G4REQ(NtUserShutdownBlockReasonQuery);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    static WCHAR s[0x110];
    for (int i = 0; i < 0x10f; i++) s[i] = L'A';
    s[0x10f] = 0;
    SET_ERR(0);
    CHECK(UN(NtUserShutdownBlockReasonCreate)(h, s, 0x102) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(UN(NtUserShutdownBlockReasonCreate)(h, s, 0xffffffff) == 0);
    DWORD n = 0; SET_ERR(0);
    CHECK(sb_query(h, NULL, &n) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* the failures stored nothing */
    CHECK(UN(NtUserShutdownBlockReasonCreate)(h, s, 0x101) == 1);
    CHECK(sb_query(h, NULL, &n) == 1); CHECKF(n == 0x102, "n=0x%lx", n);
    g4_destroy(h);
}
/* user32!ShutdownBlockReasonCreate caps at 0x100 characters (scan of 0x101 for a NUL) and rejects NULL, both with 0x57. */
TEST(gap4_shutdown_reason_user32_wrapper_limits) {
    REQUIRE(U(ShutdownBlockReasonCreate)); G4REQ(NtUserShutdownBlockReasonQuery);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    static WCHAR s[0x110];
    for (int i = 0; i < 0x10f; i++) s[i] = L'B';
    s[0x10f] = 0;
    SET_ERR(0); CHECK(U(ShutdownBlockReasonCreate)(h, NULL) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(U(ShutdownBlockReasonCreate)(h, s) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    s[0x101] = 0;
    SET_ERR(0); CHECK(U(ShutdownBlockReasonCreate)(h, s) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* 0x101 characters */
    s[0x100] = 0;
    CHECK(U(ShutdownBlockReasonCreate)(h, s) == 1);                                                   /* 0x100 characters */
    DWORD n = 0; CHECK(sb_query(h, NULL, &n) == 1); CHECK(n == 0x101);
    g4_destroy(h);
}
/* Only a top-level window (GetAncestor(GA_ROOT) == itself) can carry a reason. */
TEST(gap4_shutdown_reason_child_window_fails_87) {
    G4REQ(NtUserShutdownBlockReasonCreate); G4REQ(NtUserShutdownBlockReasonQuery);
    HWND p = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    HWND c = p ? g4_window(WS_CHILD, 0, 0, 0, 40, 40, p) : NULL;
    if (!c) { t_skip("no windows"); g4_destroy(p); return; }
    SET_ERR(0);
    CHECK(sb_create(c, L"child") == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    DWORD n = 0; CHECK(sb_query(c, NULL, &n) == 0);
    g4_destroy(p);
}
TEST(gap4_shutdown_reason_owned_popup_is_top_level) {
    G4REQ(NtUserShutdownBlockReasonCreate); G4REQ(NtUserShutdownBlockReasonQuery);
    HWND p = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    HWND o = p ? g4_window(WS_POPUP, 0, 0, 0, 40, 40, p) : NULL;   /* owned, not a child */
    if (!o) { t_skip("no windows"); g4_destroy(p); return; }
    CHECK(sb_create(o, L"owned") == 1);
    WCHAR buf[16]; DWORD n = 16; CHECK(sb_query(o, buf, &n) == 1); EXPECT_WSTR(buf, L"owned");
    DWORD m = 0; CHECK(sb_query(p, NULL, &m) == 0);   /* the owner is untouched */
    g4_destroy(p);
}
TEST(gap4_shutdown_reason_destroy_removes_and_then_fails_87) {
    G4REQ(NtUserShutdownBlockReasonCreate); G4REQ(NtUserShutdownBlockReasonQuery); G4REQ(NtUserShutdownReasonDestroy);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    SET_ERR(0); CHECK(UN(NtUserShutdownReasonDestroy)(h) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* nothing to remove */
    CHECK(sb_create(h, L"gone") == 1);
    CHECK(UN(NtUserShutdownReasonDestroy)(h) == 1);
    DWORD n = 0; SET_ERR(0);
    CHECK(sb_query(h, NULL, &n) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(UN(NtUserShutdownReasonDestroy)(h) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    g4_destroy(h);
}
/* Reasons are per window. */
TEST(gap4_shutdown_reason_per_window) {
    G4REQ(NtUserShutdownBlockReasonCreate); G4REQ(NtUserShutdownBlockReasonQuery); G4REQ(NtUserShutdownReasonDestroy);
    HWND a = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    HWND b = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!a || !b) { t_skip("no windows"); g4_destroy(a); g4_destroy(b); return; }
    WCHAR buf[16]; DWORD n = 16;
    CHECK(sb_create(a, L"A") == 1); CHECK(sb_create(b, L"B") == 1);
    CHECK(UN(NtUserShutdownReasonDestroy)(a) == 1);
    CHECK(sb_query(b, buf, &n) == 1); EXPECT_WSTR(buf, L"B");
    g4_destroy(a); g4_destroy(b);
}
/* A window of another process: for a caller at High integrity both Create and Destroy return the bare value 5 (not FALSE)
 * and set no error. A caller below High integrity gets the same bare 5 for the desktop window (owned by the system) but
 * FALSE with ERROR_ACCESS_DENIED for the windows of the user's shell. Query has no ownership check and fails with 0x57 when
 * the foreign window carries no reason. */
static int g4_high_integrity(void) {
    HANDLE tok; BYTE buf[256]; DWORD n = 0; int high = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return 0;
    if (GetTokenInformation(tok, TokenIntegrityLevel, buf, sizeof buf, &n)) {
        PSID sid = ((TOKEN_MANDATORY_LABEL *)buf)->Label.Sid;
        high = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1) >= SECURITY_MANDATORY_HIGH_RID;
    }
    CloseHandle(tok);
    return high;
}
TEST(gap4_shutdown_reason_foreign_window_returns_5) {
    G4REQ(NtUserShutdownBlockReasonCreate); G4REQ(NtUserShutdownReasonDestroy); G4REQ(NtUserShutdownBlockReasonQuery);
    REQUIRE(U(GetShellWindow)); REQUIRE(U(GetDesktopWindow));
    HWND wins[2] = { U(GetDesktopWindow)(), U(GetShellWindow)() };
    int high = g4_high_integrity();
    for (int i = 0; i < 2; i++) {
        if (!wins[i]) { t_note("no window %d", i); continue; }
        DWORD pid = 0; if (U(GetWindowThreadProcessId)) U(GetWindowThreadProcessId)(wins[i], &pid);
        if (pid == GetCurrentProcessId()) { t_note("window %d is ours", i); continue; }
        int bare5 = high || i == 0;
        SET_ERR(0xBEEF);
        int r = sb_create(wins[i], L"nope");
        DWORD e = GetLastError();
        if (bare5) { CHECKF(r == 5, "create window %p ret %d", wins[i], r); CHECKF(e == 0xBEEF, "create window %p set error %lu", wins[i], e); }
        else { CHECKF(r == 0, "create window %p ret %d", wins[i], r); CHECKF(e == ERROR_ACCESS_DENIED, "create window %p error %lu", wins[i], e); }
        SET_ERR(0xBEEF);
        r = UN(NtUserShutdownReasonDestroy)(wins[i]);
        e = GetLastError();
        if (bare5) { CHECKF(r == 5, "destroy window %p ret %d", wins[i], r); CHECKF(e == 0xBEEF, "destroy window %p set error %lu", wins[i], e); }
        else { CHECKF(r == 0, "destroy window %p ret %d", wins[i], r); CHECKF(e == ERROR_ACCESS_DENIED, "destroy window %p error %lu", wins[i], e); }
    }
}

/* ---- Syscall stubs of the calls that are deliberately never invoked with valid arguments --------- */
TEST(gap4_stub_service_numbers) {
    static const struct { const char *name; long num; } v[] = {
        { "NtUserOpenDesktop", 4256 }, { "NtUserPaintDesktop", 4360 }, { "NtUserCalculatePopupWindowPosition", 5029 },
        { "NtUserDisplayConfigSetDeviceInfo", 5073 }, { "NtUserDragDetect", 5078 }, { "NtUserDrawAnimatedRects", 5080 },
        { "NtUserDrawCaption", 5081 }, { "NtUserEnableNonClientDpiScaling", 5094 }, { "NtUserEndMenu", 5101 },
        { "NtUserGetAutoRotationState", 5109 }, { "NtUserGetDisplayAutoRotationPreferences", 5122 },
        { "NtUserGetListBoxInfo", 5146 }, { "NtUserGetMouseMovePointsEx", 5149 }, { "NtUserGetWindowFeedbackSetting", 5192 },
        { "NtUserLockWorkStation", 5239 }, { "NtUserLogicalToPhysicalPoint", 5242 }, { "NtUserPhysicalToLogicalPoint", 5262 },
        { "NtUserRemoveInjectionDevice", 5299 }, { "NtUserSetDisplayAutoRotationPreferences", 5323 },
        { "NtUserSetDisplayConfig", 5324 }, { "NtUserSetSystemCursor", 5357 }, { "NtUserSetWindowFeedbackSetting", 5367 },
        { "NtUserShutdownBlockReasonCreate", 5374 }, { "NtUserShutdownBlockReasonQuery", 5375 }, { "NtUserShutdownReasonDestroy", 5376 },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        FARPROC p = t_proc_in("win32u.dll", v[i].name);
        CHECKF(p != NULL, "%s not exported by win32u.dll", v[i].name);
        if (!p) continue;
        if (t_is_wine()) { t_note("%s number=%ld (Wine's own table)", v[i].name, k_stub_number(p)); continue; }
        CHECKF(k_stub_valid(p), "%s stub prologue", v[i].name);
        CHECKF(k_stub_number(p) == v[i].num, "%s got %ld want %ld", v[i].name, k_stub_number(p), v[i].num);
    }
}
/* user32 exports that are plain jump thunks to the syscall stub resolve to code inside user32, not win32u. */
TEST(gap4_user32_thunks_exist) {
    static const char *n[] = { "DragDetect", "DrawAnimatedRects", "DrawCaption", "EnableNonClientDpiScaling", "EndMenu",
        "GetAutoRotationState", "GetDisplayAutoRotationPreferences", "GetListBoxInfo", "GetMouseMovePointsEx",
        "GetWindowFeedbackSetting", "LockWorkStation", "LogicalToPhysicalPoint", "PaintDesktop", "PhysicalToLogicalPoint",
        "SetDisplayAutoRotationPreferences", "SetSystemCursor", "SetWindowFeedbackSetting", "ShutdownBlockReasonCreate",
        "ShutdownBlockReasonQuery", "ShutdownBlockReasonDestroy", "CalculatePopupWindowPosition", "DisplayConfigSetDeviceInfo",
        "SetDisplayConfig", "RemoveInjectionDevice", "OpenDesktopW", "OpenDesktopA" };
    for (unsigned i = 0; i < sizeof n / sizeof n[0]; i++) {
        if (t_is_wine()) { if (!t_proc_in("user32.dll", n[i])) t_note("Wine user32 lacks %s", n[i]); continue; }
        CHECKF(t_proc_in("user32.dll", n[i]) != NULL, "user32!%s", n[i]);
    }
}
