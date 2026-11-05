/* x_ntqip_qip_basic_quota_handles / t_watch: the working set watch classes
 *   15 (records of 0x10 bytes: faulting code address, faulting address), 42 (records of 0x20 bytes: code address,
 *   faulting address, thread id, flags).
 * Measured on Windows 10 22H2 x64:
 *   - both classes need PROCESS_QUERY_INFORMATION; the limited right is STATUS_ACCESS_DENIED.
 *   - a process whose watch was never started answers STATUS_UNSUCCESSFUL for any length (also 0); class 15 looks
 *     at the handle before the length (an invalid handle with a bad length is STATUS_INVALID_HANDLE, a limited
 *     handle STATUS_ACCESS_DENIED), class 42 checks the length first: only multiples of 0x20 pass (0 included),
 *     everything else is STATUS_INFO_LENGTH_MISMATCH.
 *   - after the watch is started and the pending records were consumed, both classes answer
 *     STATUS_NO_MORE_ENTRIES for every length (class 42 for lengths that are multiples of 0x20).
 *   - with records pending, a buffer that is too small is STATUS_BUFFER_TOO_SMALL with ReturnLength = the
 *     needed size (all records plus a terminating record) and nothing written, the records stay pending; a large enough
 *     buffer succeeds, ReturnLength is the used size, the last record is all zero and the list is consumed. Fresh committed pages that the
 *     process touches show up as records.
 *   - a caller running with low integrity is refused with STATUS_ACCESS_DENIED before anything else is looked at,
 *     with the pseudo handle and with a real handle.
 */
#include "qb.h"
#include <sddl.h>

static ULONGLONG rd64(const unsigned char *p, ULONG off) { ULONGLONG v; memcpy(&v, p + off, 8); return v; }

static void drain(void) {
    for (int i = 0; i < 8; i++) {
        ULONG rl;
        LONG s = qb_call(PSEUDO_SELF, 15, 0, 0x1000, 0, 0, &rl);
        if (s == ST_NOMORE) return;
    }
}
static int start_watch(void) {
    CHECK(1); CHECKF(1, "warm %d", 1); EXPECT_EQ(0, 0);   /* touch the assertion code before the watch begins */
    LONG s = qb_set(PSEUDO_SELF, 15, NULL, 0);
    /* STATUS_PORT_ALREADY_SET: an earlier test of this process already started it */
    if (s != 0 && s != (LONG)0xC0000048) { t_skip("starting the watch failed with 0x%lx", (ULONG)s); return 0; }
    drain();
    return 1;
}

/* ---- watch not started ------------------------------------------------- */
TEST(c15_c42_not_started) {
    REQUIRE(qb_have());
    DWORD pid;
    HANDLE c = qb_fresh_child(&pid);
    REQUIRE(c);
    HANDLE q = OpenProcess(R_QRY, FALSE, pid), l = OpenProcess(R_LIM, FALSE, pid), s = OpenProcess(R_SYNC, FALSE, pid);
    static const ULONG lens[] = { 0, 1, 3, 4, 8, 0x10, 0x18, 0x1f, 0x20, 0x28, 0x40, 0x60, 0x100, 0x1000 };
    for (unsigned i = 0; i < sizeof lens / sizeof *lens; i++) {
        ULONG len = lens[i], rl;
        LONG st = qb_call(c, 15, 0, len, 0, 0, &rl);
        CHECKF(st == ST_UNSUCC && rl == QB_SENT, "cls 15 len 0x%lx all access: 0x%lx", len, (ULONG)st);
        st = qb_call(q, 15, 0, len, 0, 0, &rl);
        CHECKF(st == ST_UNSUCC, "cls 15 len 0x%lx query right: 0x%lx", len, (ULONG)st);
        st = qb_call(l, 15, 0, len, 0, 0, &rl);
        CHECKF(st == ST_ACCESS, "cls 15 len 0x%lx limited right: 0x%lx", len, (ULONG)st);
        st = qb_call(s, 15, 0, len, 0, 0, &rl);
        CHECKF(st == ST_ACCESS, "cls 15 len 0x%lx sync only: 0x%lx", len, (ULONG)st);
        LONG want42 = (len % 0x20 == 0) ? ST_UNSUCC : ST_LENMIS;
        st = qb_call(q, 42, 0, len, 0, 0, &rl);
        CHECKF(st == want42 && rl == QB_SENT, "cls 42 len 0x%lx query right: 0x%lx want 0x%lx", len, (ULONG)st, (ULONG)want42);
        LONG wantl = (len % 0x20 == 0) ? ST_ACCESS : ST_LENMIS;
        st = qb_call(l, 42, 0, len, 0, 0, &rl);
        CHECKF(st == wantl, "cls 42 len 0x%lx limited right: 0x%lx want 0x%lx", len, (ULONG)st, (ULONG)wantl);
    }
    CloseHandle(q); CloseHandle(l); CloseHandle(s);
    TerminateProcess(c, 0); CloseHandle(c);
}
TEST(c15_c42_handle_and_length_order) {
    REQUIRE(qb_have());
    ULONG rl;
    HANDLE l = qb_child_open(R_LIM);
    REQUIRE(l);
    /* class 15: handle first */
    EXPECT_EQ(qb_call(NULL, 15, 0, 1, 0, 0, &rl), ST_BADHANDLE);
    EXPECT_EQ(qb_call(PSEUDO_THREAD, 15, 0, 3, 0, 0, &rl), ST_TYPEMIS);
    EXPECT_EQ(qb_call(l, 15, 0, 1, 0, 0, &rl), ST_ACCESS);
    EXPECT_EQ(qb_call(qb_closed_handle(), 15, 0, 0x10, 0, 0, &rl), ST_BADHANDLE);
    /* class 42: length first */
    EXPECT_EQ(qb_call(NULL, 42, 0, 1, 0, 0, &rl), ST_LENMIS);
    EXPECT_EQ(qb_call(PSEUDO_THREAD, 42, 0, 3, 0, 0, &rl), ST_LENMIS);
    EXPECT_EQ(qb_call(l, 42, 0, 0x10, 0, 0, &rl), ST_LENMIS);
    EXPECT_EQ(qb_call(NULL, 42, 0, 0x20, 0, 0, &rl), ST_BADHANDLE);
    EXPECT_EQ(qb_call(PSEUDO_THREAD, 42, 0, 0x20, 0, 0, &rl), ST_TYPEMIS);
    EXPECT_EQ(qb_call(NULL, 42, 0, 0, 0, 0, &rl), ST_BADHANDLE);
    CloseHandle(l);
}
TEST(c15_c42_null_arguments_not_started) {
    REQUIRE(qb_have());
    DWORD pid;
    HANDLE c = qb_fresh_child(&pid);
    REQUIRE(c);
    ULONG rl = QB_SENT;
    EXPECT_EQ(qb_q(c, 15, NULL, 0, &rl), ST_UNSUCC); EXPECT_EQ(rl, QB_SENT);
    EXPECT_EQ(qb_q(c, 15, NULL, 0, NULL), ST_UNSUCC);
    EXPECT_EQ(qb_q(c, 42, NULL, 0, &rl), ST_UNSUCC);
    EXPECT_EQ(qb_q(c, 42, NULL, 0, NULL), ST_UNSUCC);
    EXPECT_EQ(qb_q(c, 15, qb_arena(), 0x10, NULL), ST_UNSUCC);
    EXPECT_EQ(qb_q(c, 42, qb_arena(), 0x20, NULL), ST_UNSUCC);
    TerminateProcess(c, 0); CloseHandle(c);
}
TEST(c15_c42_exited_process) {
    REQUIRE(qb_have());
    HANDLE d = qb_dead();
    REQUIRE(d);
    ULONG rl;
    EXPECT_EQ(qb_call(d, 15, 0, 0x100, 0, 0, &rl), ST_UNSUCC);
    EXPECT_EQ(qb_call(d, 15, 0, 0, 0, 0, &rl), ST_UNSUCC);
    EXPECT_EQ(qb_call(d, 42, 0, 0x100, 0, 0, &rl), ST_UNSUCC);
    EXPECT_EQ(qb_call(d, 42, 0, 0x11, 0, 0, &rl), ST_LENMIS);
}

/* ---- watch started ----------------------------------------------------- */
TEST(c15_c42_no_more_entries) {
    REQUIRE(qb_have());
    if (!start_watch()) return;
    static const ULONG lens[] = { 0, 1, 3, 8, 0x10, 0x20, 0x30, 0x400 };
    for (unsigned i = 0; i < sizeof lens / sizeof *lens; i++) {
        ULONG rl;
        LONG s = qb_call(PSEUDO_SELF, 15, 0, lens[i], 0, 0, &rl);
        CHECKF(s == ST_NOMORE && rl == QB_SENT && qb_untouched(0, 0x100), "cls 15 len 0x%lx: 0x%lx", lens[i], (ULONG)s);
        LONG want = (lens[i] % 0x20 == 0) ? ST_NOMORE : ST_LENMIS;
        s = qb_call(PSEUDO_SELF, 42, 0, lens[i], 0, 0, &rl);
        CHECKF(s == want && rl == QB_SENT, "cls 42 len 0x%lx: 0x%lx want 0x%lx", lens[i], (ULONG)s, (ULONG)want);
    }
    ULONG rl;
    EXPECT_EQ(qb_q(PSEUDO_SELF, 15, NULL, 0, NULL), ST_NOMORE);
    EXPECT_EQ(qb_q(PSEUDO_SELF, 42, NULL, 0, &rl), ST_NOMORE);
}
TEST(c15_records_for_touched_pages) {
    REQUIRE(qb_have());
    if (!start_watch()) return;
    enum { N = 16 };
    char *m = VirtualAlloc(NULL, N * 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(m);
    for (int i = 0; i < N; i++) m[i * 0x1000] = 1;
    ULONG rl0;
    LONG s = qb_call(PSEUDO_SELF, 15, 0, 0, 0, 0, &rl0);
    EXPECT_EQ(s, ST_SMALL);
    CHECKF(rl0 % 0x10 == 0 && rl0 >= (N + 1) * 0x10, "needed size 0x%lx", rl0);
    EXPECT_EQ(qb_untouched(0, 0x100), 1);
    ULONG rl;
    s = qb_call(PSEUDO_SELF, 15, 0, rl0 - 0x10, 0, 0, &rl);
    CHECKF(s == ST_SMALL && rl == rl0, "one record short: 0x%lx rl 0x%lx", (ULONG)s, rl);
    EXPECT_EQ(qb_untouched(0, 0x400), 1);
    /* the same pending set seen through the wide records is twice as large */
    ULONG rl42;
    s = qb_call(PSEUDO_SELF, 42, 0, 0, 0, 0, &rl42);
    CHECKF(s == ST_SMALL && rl42 == 2 * rl0, "class 42 needed 0x%lx class 15 needed 0x%lx", rl42, rl0);
    s = qb_call(PSEUDO_SELF, 15, 0, rl0 - 1, 0, 0, &rl);
    CHECKF(s == ST_SMALL && rl == rl0, "len-1: 0x%lx rl 0x%lx", (ULONG)s, rl);
    /* a large enough buffer consumes the list */
    s = qb_call(PSEUDO_SELF, 15, 0, rl0, 0, 0, &rl);
    CHECKF(s == 0 && rl == rl0, "exact: 0x%lx rl 0x%lx", (ULONG)s, rl);
    const unsigned char *a = qb_arena();
    int seen = 0;
    for (int i = 0; i < N; i++) {
        ULONGLONG page = (ULONGLONG)(ULONG_PTR)m + i * 0x1000;
        for (ULONG r = 0; r + 0x10 < rl; r += 0x10) if ((rd64(a, r + 8) & ~0xfffULL) == page) { seen++; break; }
    }
    EXPECT_EQ(seen, N);
    CHECKF(rd64(a, rl - 0x10) == 0 && rd64(a, rl - 8) == 0, "terminator record not zero: %llx %llx", rd64(a, rl - 0x10), rd64(a, rl - 8));
    for (ULONG r = 0; r + 0x10 < rl; r += 0x10) if (rd64(a, r) == 0 && rd64(a, r + 8) == 0) { CHECKF(0, "record %lu is all zero before the end", r / 0x10); break; }
    CHECK(qb_untouched(rl, rl + 16));
    /* consumed: nothing left, apart from records of faults the checks above may have caused */
    s = qb_call(PSEUDO_SELF, 15, 0, 0x400, 0, 0, &rl);
    CHECKF(s == ST_NOMORE || s == 0, "after consumption: 0x%lx", (ULONG)s);
    VirtualFree(m, 0, MEM_RELEASE);
}
TEST(c42_records_carry_thread_id) {
    REQUIRE(qb_have());
    if (!start_watch()) return;
    enum { N = 8 };
    char *m = VirtualAlloc(NULL, N * 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(m);
    for (int i = 0; i < N; i++) m[i * 0x1000] = 1;
    ULONG rl0, rl;
    EXPECT_EQ(qb_call(PSEUDO_SELF, 42, 0, 0x20, 0, 0, &rl0), ST_SMALL);
    CHECKF(rl0 % 0x20 == 0 && rl0 >= (N + 1) * 0x20, "needed size 0x%lx", rl0);
    EXPECT_EQ(qb_call(PSEUDO_SELF, 42, 0, rl0 - 0x20, 0, 0, &rl), ST_SMALL);
    EXPECT_EQ(rl, rl0);
    EXPECT_EQ(qb_call(PSEUDO_SELF, 42, 0, rl0, 0, 0, &rl), 0);
    EXPECT_EQ(rl, rl0);
    const unsigned char *a = qb_arena();
    int seen = 0;
    for (int i = 0; i < N; i++) {
        ULONGLONG page = (ULONGLONG)(ULONG_PTR)m + i * 0x1000;
        for (ULONG r = 0; r + 0x20 < rl; r += 0x20)
            if ((rd64(a, r + 8) & ~0xfffULL) == page) {
                seen++;
                CHECKF(rd64(a, r + 0x10) == GetCurrentThreadId(), "record %lu thread id 0x%llx want 0x%lx", r / 0x20, rd64(a, r + 0x10), GetCurrentThreadId());
                CHECKF(rd64(a, r) != 0, "record %lu has no code address", r / 0x20);
                break;
            }
    }
    EXPECT_EQ(seen, N);
    CHECKF(!rd64(a, rl - 0x20) && !rd64(a, rl - 0x18) && !rd64(a, rl - 0x10) && !rd64(a, rl - 8), "terminator record not zero");
    LONG s2 = qb_call(PSEUDO_SELF, 42, 0, 0x400, 0, 0, &rl);
    CHECKF(s2 == ST_NOMORE || s2 == 0, "after consumption: 0x%lx", (ULONG)s2);
    VirtualFree(m, 0, MEM_RELEASE);
}
TEST(c15_c42_rights_with_watch_started) {
    REQUIRE(qb_have());
    if (!start_watch()) return;
    DWORD pid = GetCurrentProcessId();
    struct { const char *n; DWORD mask; LONG want; } t[] = {
        { "lim", R_LIM, ST_ACCESS }, { "sync", R_SYNC, ST_ACCESS }, { "vmr", R_VMR, ST_ACCESS }, { "dup", R_DUP, ST_ACCESS },
        { "qry", R_QRY, ST_NOMORE }, { "lim+qry", R_LIM | R_QRY, ST_NOMORE }, { "all", PROCESS_ALL_ACCESS, ST_NOMORE },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        HANDLE h = OpenProcess(t[i].mask, FALSE, pid);
        if (!h) { t_note("%s: OpenProcess failed", t[i].n); continue; }
        ULONG rl;
        LONG s = qb_call(h, 15, 0, 0x100, 0, 0, &rl);
        CHECKF(s == t[i].want, "cls 15 %s: 0x%lx want 0x%lx", t[i].n, (ULONG)s, (ULONG)t[i].want);
        s = qb_call(h, 42, 0, 0x100, 0, 0, &rl);
        CHECKF(s == t[i].want, "cls 42 %s: 0x%lx want 0x%lx", t[i].n, (ULONG)s, (ULONG)t[i].want);
        CloseHandle(h);
    }
}
TEST(c15_start_watch_on_other_process) {
    REQUIRE(qb_have());
    DWORD pid;
    HANDLE c = qb_fresh_child(&pid);
    REQUIRE(c);
    ULONG rl;
    EXPECT_EQ(qb_call(c, 15, 0, 0x100, 0, 0, &rl), ST_UNSUCC);
    LONG s = qb_set(c, 15, NULL, 0);
    if (s != 0) { t_skip("start on another process refused: 0x%lx", (ULONG)s); TerminateProcess(c, 0); CloseHandle(c); return; }
    s = qb_call(c, 15, 0, 0x1000, 0, 0, &rl);
    CHECKF(s == ST_NOMORE || s == 0, "status 0x%lx after start", (ULONG)s);   /* an idle process has no, or only a few, records */
    CHECK(s != ST_UNSUCC);
    s = qb_call(c, 42, 0, 0x1000, 0, 0, &rl);
    CHECKF(s == ST_NOMORE || s == 0, "status 0x%lx after start", (ULONG)s);
    TerminateProcess(c, 0); CloseHandle(c);
}

/* ---- restricted caller --------------------------------------------------- */
CHILD(qblow) {   /* runs with a low integrity token; returns the number of mismatches */
    int bad = 0;
    ULONG rl;
    HANDLE self = (HANDLE)(LONG_PTR)-1;
    HANDLE real = OpenProcess(R_QRY, FALSE, GetCurrentProcessId());
    printf("integrity %lu\n", (unsigned long)k_integrity_level());
    struct { const char *n; HANDLE h; ULONG cls, len; LONG want; } t[] = {
        { "15 pseudo", self, 15, 0x10, ST_ACCESS }, { "15 pseudo len0", self, 15, 0, ST_ACCESS },
        { "42 pseudo", self, 42, 0x20, ST_ACCESS }, { "42 pseudo len0", self, 42, 0, ST_ACCESS },
        { "15 real", real, 15, 0x100, ST_ACCESS }, { "42 real", real, 42, 0x100, ST_ACCESS },
        { "0 pseudo", self, 0, 0x30, ST_OK }, { "20 pseudo", self, 20, 4, ST_OK }, { "4 pseudo", self, 4, 0x20, ST_OK },
        { "3 pseudo", self, 3, 0x58, ST_OK }, { "38 pseudo", self, 38, 0x10, ST_OK },
        { "51 pseudo", self, 51, 0x10, ST_LENMIS }, { "58 pseudo", self, 58, 0x100, ST_OK },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        LONG s = qb_call(t[i].h, t[i].cls, 0, t[i].len, 0, 0, &rl);
        if (!t[i].h && strstr(t[i].n, "real")) continue;
        if (s != t[i].want) { printf("MISMATCH %s: 0x%lx want 0x%lx\n", t[i].n, (ULONG)s, (ULONG)t[i].want); bad++; }
    }
    return bad;
}
TEST(c15_c42_low_integrity_caller_refused) {
    REQUIRE(qb_have());
    HANDLE tok, dup = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ASSIGN_PRIMARY, &tok)) { t_skip("cannot open own token"); return; }
    if (!DuplicateTokenEx(tok, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &dup)) { CloseHandle(tok); t_skip("cannot duplicate token"); return; }
    CloseHandle(tok);
    PSID sid = NULL;
    if (!ConvertStringSidToSidA("S-1-16-4096", &sid)) { CloseHandle(dup); t_skip("no low integrity sid"); return; }
    TOKEN_MANDATORY_LABEL tml;
    tml.Label.Sid = sid; tml.Label.Attributes = SE_GROUP_INTEGRITY;
    BOOL ok = SetTokenInformation(dup, TokenIntegrityLevel, &tml, sizeof tml + GetLengthSid(sid));
    LocalFree(sid);
    if (!ok) { CloseHandle(dup); t_skip("cannot lower token integrity"); return; }
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd, wr;
    CreatePipe(&rd, &wr, &sa, 0);
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si; memset(&si, 0, sizeof si);
    si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    char cmd[600];
    snprintf(cmd, sizeof cmd, "\"%s\" --child qblow", t_self_path());
    PROCESS_INFORMATION pi;
    BOOL c = CreateProcessAsUserA(dup, NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    DWORD err = GetLastError();
    CloseHandle(wr); CloseHandle(dup);
    if (!c) { CloseHandle(rd); t_skip("cannot start a low integrity process (error %lu)", err); return; }
    char out[2048]; DWORD n, tot = 0;
    CloseHandle(pi.hThread);
    while (tot < sizeof out - 1 && ReadFile(rd, out + tot, sizeof out - 1 - tot, &n, NULL) && n) tot += n;
    out[tot] = 0;
    CloseHandle(rd);
    DWORD ec = 0xdead;
    if (WaitForSingleObject(pi.hProcess, 30000) != WAIT_OBJECT_0) { TerminateProcess(pi.hProcess, 1); t_skip("low integrity process hung"); CloseHandle(pi.hProcess); return; }
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);
    if (!strstr(out, "integrity 4096")) { t_skip("child did not run at low integrity: %.60s", out); return; }
    CHECKF(ec == 0, "low integrity child reported %lu mismatches: %s", ec, out);
}
