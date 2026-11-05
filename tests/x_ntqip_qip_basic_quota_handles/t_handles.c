/* x_ntqip_qip_basic_quota_handles / t_handles: the handle classes
 *   51 handle snapshot: a 0x10 byte header (NumberOfHandles, Reserved) followed by 0x28 byte entries
 *      (HandleValue, HandleCount, PointerCount, GrantedAccess, ObjectTypeIndex, HandleAttributes, Reserved),
 *   58 handle table: a plain array of 32 bit handle values.
 * Measured on Windows 10 22H2 x64:
 *   - class 51 needs PROCESS_QUERY_INFORMATION (the limited right is STATUS_ACCESS_DENIED); the pseudo handle
 *     needs nothing. A buffer shorter than 0x10 gives STATUS_INFO_LENGTH_MISMATCH with ReturnLength = 0x10 and
 *     nothing written; a larger but too small buffer gives STATUS_INFO_LENGTH_MISMATCH with ReturnLength = the full
 *     size (0x10 + 0x28 * handles); whole entries that fit are written (together with the header) only when at
 *     least one fits. A buffer that is large enough succeeds, ReturnLength is the used size and the header's
 *     reserved qword is not written. An exited process is STATUS_PROCESS_IS_TERMINATING.
 *   - class 58 needs PROCESS_QUERY_INFORMATION | PROCESS_DUP_HANDLE (0x440) for a real handle. Any length of at
 *     least 4 succeeds: the output holds floor(len / 4) values at most, ReturnLength is the number of bytes
 *     written (4 * values); a shorter buffer is silently truncated. Length below 4 is the length error. An exited
 *     process is STATUS_PROCESS_IS_TERMINATING and ReturnLength is set to 0.
 *   - a truncated snapshot carries the number of stored entries in its header.
 *   - both lists describe the same handle set as class 20's count.
 */
#include "qb.h"

#define ENT 0x28
static ULONGLONG rd64(const unsigned char *p, ULONG off) { ULONGLONG v; memcpy(&v, p + off, 8); return v; }
static ULONG rd32(const unsigned char *p, ULONG off) { ULONG v; memcpy(&v, p + off, 4); return v; }

static ULONG handle_count(HANDLE h) {
    ULONG rl;
    if (qb_call(h, 20, 0, 4, 0, 0, &rl) != 0) return 0xffffffff;
    return qb_u32(0);
}
static LONG snap(HANDLE h, ULONG len, ULONG *rl) {
    unsigned char *b = qb_big();
    memset(b, QB_FILL, QB_BIG_SIZE);
    *rl = QB_SENT;
    return qb_q(h, 51, b, len, rl);
}
static LONG table(HANDLE h, ULONG len, ULONG *rl) {
    unsigned char *b = qb_big();
    memset(b, QB_FILL, QB_BIG_SIZE);
    *rl = QB_SENT;
    return qb_q(h, 58, b, len, rl);
}
static const unsigned char *find_entry(ULONG_PTR hv) {
    const unsigned char *b = qb_big();
    ULONGLONG n = rd64(b, 0);
    for (ULONGLONG i = 0; i < n; i++) if (rd64(b, 0x10 + i * ENT) == hv) return b + 0x10 + i * ENT;
    return NULL;
}

/* ---- class 51 ---------------------------------------------------------- */
TEST(c51_snapshot_header_and_entries) {
    REQUIRE(qb_have());
    ULONG rl;
    ULONG n = handle_count(PSEUDO_SELF);
    LONG s = snap(PSEUDO_SELF, QB_BIG_SIZE, &rl);
    EXPECT_EQ(s, 0);
    const unsigned char *b = qb_big();
    ULONGLONG cnt = rd64(b, 0);
    EXPECT_EQ(cnt, n);
    EXPECT_EQ(rl, 0x10 + cnt * ENT);
    EXPECT_EQ(rd64(b, 8), 0xccccccccccccccccULL);                 /* Reserved is not written */
    CHECK(cnt >= 3);
    ULONGLONG prev = 0;
    for (ULONGLONG i = 0; i < cnt; i++) {
        const unsigned char *e = b + 0x10 + i * ENT;
        ULONGLONG hv = rd64(e, 0);
        CHECKF(hv != 0 && (hv & 3) == 0 && hv > prev, "entry %llu handle value 0x%llx after 0x%llx", i, hv, prev);
        prev = hv;
        CHECKF(rd64(e, 8) >= 1, "entry %llu HandleCount %llu", i, rd64(e, 8));
        CHECKF(rd64(e, 0x10) >= 1, "entry %llu PointerCount %llu", i, rd64(e, 0x10));
        CHECKF(rd32(e, 0x18) != 0, "entry %llu GrantedAccess 0", i);
        CHECKF(rd32(e, 0x1c) != 0 && rd32(e, 0x1c) < 0x100, "entry %llu ObjectTypeIndex %lu", i, rd32(e, 0x1c));
        CHECKF(rd32(e, 0x24) == 0, "entry %llu reserved dword 0x%lx", i, rd32(e, 0x24));
    }
    /* nothing is written behind the reported size */
    for (ULONG i = rl; i < rl + 32; i++) if (b[i] != QB_FILL) { CHECKF(0, "byte 0x%lx behind the snapshot written", i); break; }
}
TEST(c51_entry_describes_event_handle) {
    REQUIRE(qb_have());
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE evi = CreateEventA(&sa, TRUE, FALSE, NULL);
    HANDLE ev2 = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE pr = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetHandleInformation(pr, HANDLE_FLAG_PROTECT_FROM_CLOSE, HANDLE_FLAG_PROTECT_FROM_CLOSE);
    ULONG rl;
    EXPECT_EQ(snap(PSEUDO_SELF, QB_BIG_SIZE, &rl), 0);
    const unsigned char *e = find_entry((ULONG_PTR)ev), *ei = find_entry((ULONG_PTR)evi), *e2 = find_entry((ULONG_PTR)ev2), *ep = find_entry((ULONG_PTR)pr);
    REQUIRE(e && ei && e2 && ep);
    EXPECT_EQ(rd32(e, 0x18), 0x1f0003);                          /* EVENT_ALL_ACCESS */
    EXPECT_EQ(rd64(e, 8), 1);                                    /* one handle to the object */
    EXPECT_EQ(rd32(e, 0x20), 0);                                 /* plain attributes */
    EXPECT_EQ(rd32(ei, 0x20), 2);                                /* inheritable */
    EXPECT_EQ(rd32(ep, 0x20), 1);                                /* protected from close */
    EXPECT_EQ(rd32(e, 0x1c), rd32(ei, 0x1c));                    /* same object type */
    EXPECT_EQ(rd32(e, 0x1c), rd32(e2, 0x1c));
    HANDLE proc = OpenProcess(R_LIM, FALSE, GetCurrentProcessId());
    HANDLE evd = NULL;
    CHECK(DuplicateHandle(GetCurrentProcess(), ev, GetCurrentProcess(), &evd, EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, 0));
    EXPECT_EQ(snap(PSEUDO_SELF, QB_BIG_SIZE, &rl), 0);
    const unsigned char *a = find_entry((ULONG_PTR)ev), *d = find_entry((ULONG_PTR)evd), *p = find_entry((ULONG_PTR)proc);
    REQUIRE(a && d && p);
    EXPECT_EQ(rd64(a, 8), 2); EXPECT_EQ(rd64(d, 8), 2);          /* both entries show the object's handle count */
    EXPECT_EQ(rd32(d, 0x18), EVENT_MODIFY_STATE | SYNCHRONIZE);
    CHECK(rd64(a, 0x10) >= rd64(a, 8));
    EXPECT_EQ(rd32(p, 0x18), R_LIM);
    CHECK(rd32(p, 0x1c) != rd32(a, 0x1c));                      /* a process is another object type */
    CloseHandle(proc); CloseHandle(evd);
    SetHandleInformation(pr, HANDLE_FLAG_PROTECT_FROM_CLOSE, 0);
    CloseHandle(ev); CloseHandle(evi); CloseHandle(ev2); CloseHandle(pr);
    /* closing removes the entry */
    EXPECT_EQ(snap(PSEUDO_SELF, QB_BIG_SIZE, &rl), 0);
    CHECK(find_entry((ULONG_PTR)ev2) == NULL);
}
TEST(c51_short_buffers) {
    REQUIRE(qb_have());
    ULONG rl, full;
    EXPECT_EQ(snap(PSEUDO_SELF, QB_BIG_SIZE, &rl), 0);
    full = rl;
    CHECK(full >= 0x10 + 3 * ENT);
    /* below the header size: length error, ReturnLength 0x10, nothing written */
    for (ULONG len = 0; len < 0x10; len++) {
        LONG s = snap(PSEUDO_SELF, len, &rl);
        CHECKF(s == ST_LENMIS && rl == 0x10, "len %lu status 0x%lx rl 0x%lx", len, (ULONG)s, rl);
        const unsigned char *b = qb_big();
        int clean = 1; for (int i = 0; i < 0x40; i++) if (b[i] != QB_FILL) clean = 0;
        CHECKF(clean, "len %lu wrote output", len);
    }
    /* up to the full size: length error with the full size reported; whole entries that fit are stored */
    for (ULONG len = 0x10; len < full; len++) {
        LONG s = snap(PSEUDO_SELF, len, &rl);
        ULONG fit = (len - 0x10) / ENT;
        CHECKF(s == ST_LENMIS && rl == full, "len 0x%lx status 0x%lx rl 0x%lx want 0x%lx", len, (ULONG)s, rl, full);
        const unsigned char *b = qb_big();
        ULONG upto = fit ? 0x10 + fit * ENT : 0;
        int ok = 1;
        for (ULONG i = upto; i < full + 16 && i < QB_BIG_SIZE; i++) if (b[i] != QB_FILL) { ok = 0; break; }
        CHECKF(ok, "len 0x%lx wrote more than %lu entries", len, fit);
        if (fit) CHECKF(rd64(b, 0x10) != 0 && rd64(b, 0) != QB_FILL, "len 0x%lx header/first entry missing", len);
    }
    /* exact size and larger */
    EXPECT_EQ(snap(PSEUDO_SELF, full, &rl), 0); EXPECT_EQ(rl, full);
    EXPECT_EQ(snap(PSEUDO_SELF, full + 1, &rl), 0); EXPECT_EQ(rl, full);
    EXPECT_EQ(snap(PSEUDO_SELF, full + 0x1000, &rl), 0); EXPECT_EQ(rl, full);
}
TEST(c51_truncated_header_count) {
    REQUIRE(qb_have());
    ULONG rl;
    EXPECT_EQ(snap(PSEUDO_SELF, QB_BIG_SIZE, &rl), 0);
    ULONGLONG n = rd64(qb_big(), 0);
    ULONG first = (ULONG)rd64(qb_big(), 0x10);
    EXPECT_EQ(snap(PSEUDO_SELF, 0x10 + 2 * ENT, &rl), ST_LENMIS);
    EXPECT_EQ(rd64(qb_big(), 0), 2);                               /* the header counts the entries that were stored */
    CHECK(n > 2);
    EXPECT_EQ(rd64(qb_big(), 0x10), first);                        /* entries come out in handle order from the start */
    EXPECT_EQ(rd64(qb_big(), 0x10 + ENT) > first, 1);
}
TEST(c51_rights_and_handle_errors) {
    REQUIRE(qb_have());
    struct { const char *n; HANDLE h; LONG want; } t[] = {
        { "lim", qb_child_open(R_LIM), ST_ACCESS }, { "sync", qb_child_open(R_SYNC), ST_ACCESS },
        { "vmr", qb_child_open(R_VMR), ST_ACCESS }, { "dup", qb_child_open(R_DUP), ST_ACCESS },
        { "qry", qb_child_open(R_QRY), ST_LENMIS }, { "qry+dup", qb_child_open(R_QRY | R_DUP), ST_LENMIS },
        { "all", qb_child_open(PROCESS_ALL_ACCESS), ST_LENMIS },
        { "zero", NULL, ST_BADHANDLE }, { "closed", qb_closed_handle(), ST_BADHANDLE },
        { "thread", PSEUDO_THREAD, ST_TYPEMIS },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        ULONG rl;
        /* a header-sized buffer is always too small for a process that has handles: the status shows the access check */
        LONG s = snap(t[i].h, 0x10, &rl);
        CHECKF(s == t[i].want, "%s: status 0x%lx want 0x%lx", t[i].n, (ULONG)s, (ULONG)t[i].want);
        if (t[i].want == ST_LENMIS) CHECKF(rl > 0x10 && (rl - 0x10) % ENT == 0, "%s: rl 0x%lx", t[i].n, rl);
        else CHECKF(rl == QB_SENT, "%s: rl 0x%lx", t[i].n, rl);
    }
    /* the length is looked at first only when it cannot hold the header at all: ReturnLength shows 0x10 even for a bad handle */
    ULONG rl;
    EXPECT_EQ(snap(NULL, 4, &rl), ST_LENMIS); EXPECT_EQ(rl, 0x10);
    EXPECT_EQ(snap(qb_child_open(R_LIM), 4, &rl), ST_LENMIS); EXPECT_EQ(rl, 0x10);
    /* a successful read of another process */
    HANDLE c = qb_child_open(R_QRY);
    if (c) {
        EXPECT_EQ(snap(c, QB_BIG_SIZE, &rl), 0);
        EXPECT_EQ(rd64(qb_big(), 0), handle_count(c));
        EXPECT_EQ(rl, 0x10 + rd64(qb_big(), 0) * ENT);
        CloseHandle(c);
    }
}
TEST(c51_null_arguments) {
    REQUIRE(qb_have());
    ULONG rl = QB_SENT;
    LONG s = qb_q(PSEUDO_SELF, 51, NULL, 0, &rl);
    EXPECT_EQ(s, ST_LENMIS); EXPECT_EQ(rl, 0x10);
    rl = QB_SENT;
    s = qb_q(PSEUDO_SELF, 51, qb_big(), 0, &rl);
    EXPECT_EQ(s, ST_LENMIS); EXPECT_EQ(rl, 0x10);
    s = qb_q(PSEUDO_SELF, 51, NULL, 0, NULL);
    EXPECT_EQ(s, ST_LENMIS);
    s = qb_q(PSEUDO_SELF, 51, qb_big(), QB_BIG_SIZE, NULL);
    EXPECT_EQ(s, 0);
}
TEST(c51_exited_process) {
    REQUIRE(qb_have());
    HANDLE d = qb_dead();
    REQUIRE(d);
    ULONG rl;
    EXPECT_EQ(snap(d, QB_BIG_SIZE, &rl), ST_TERMING); EXPECT_EQ(rl, QB_SENT);
    EXPECT_EQ(snap(d, 0x10, &rl), ST_TERMING);
    EXPECT_EQ(snap(d, 4, &rl), ST_LENMIS);
}

/* ---- class 58 ---------------------------------------------------------- */
TEST(c58_table_is_ascending_values) {
    REQUIRE(qb_have());
    ULONG rl;
    ULONG n = handle_count(PSEUDO_SELF);
    EXPECT_EQ(table(PSEUDO_SELF, QB_BIG_SIZE, &rl), 0);
    EXPECT_EQ(rl, 4 * n);
    const unsigned char *b = qb_big();
    ULONG prev = 0;
    for (ULONG i = 0; i < n; i++) {
        ULONG v = rd32(b, i * 4);
        CHECKF(v != 0 && (v & 3) == 0 && v > prev, "value %lu = 0x%lx after 0x%lx", i, v, prev);
        prev = v;
    }
    int clean = 1; for (ULONG i = rl; i < rl + 32; i++) if (b[i] != QB_FILL) clean = 0;
    CHECK(clean);
}
TEST(c58_table_agrees_with_snapshot) {
    REQUIRE(qb_have());
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    ULONG rl, rl2;
    EXPECT_EQ(table(PSEUDO_SELF, QB_BIG_SIZE, &rl), 0);
    ULONG tab[512]; ULONG cnt = rl / 4; REQUIRE(cnt <= 512);
    memcpy(tab, qb_big(), rl);
    EXPECT_EQ(snap(PSEUDO_SELF, QB_BIG_SIZE, &rl2), 0);
    EXPECT_EQ(rd64(qb_big(), 0), cnt);
    int same = 1;
    for (ULONG i = 0; i < cnt; i++) if (rd64(qb_big(), 0x10 + i * ENT) != tab[i]) same = 0;
    CHECK(same);
    int found = 0; for (ULONG i = 0; i < cnt; i++) if (tab[i] == (ULONG)(ULONG_PTR)ev) found = 1;
    CHECK(found);
    CloseHandle(ev);
    EXPECT_EQ(table(PSEUDO_SELF, QB_BIG_SIZE, &rl), 0);
    EXPECT_EQ(rl, 4 * (cnt - 1));
}
TEST(c58_truncation_is_silent) {
    REQUIRE(qb_have());
    ULONG rl, full;
    EXPECT_EQ(table(PSEUDO_SELF, QB_BIG_SIZE, &rl), 0);
    full = rl;
    ULONG ref[512]; REQUIRE(full / 4 <= 512);
    memcpy(ref, qb_big(), full);
    for (ULONG len = 0; len < 4; len++) {
        LONG s = table(PSEUDO_SELF, len, &rl);
        CHECKF(s == ST_LENMIS && rl == QB_SENT, "len %lu status 0x%lx rl 0x%lx", len, (ULONG)s, rl);
    }
    for (ULONG len = 4; len <= 0x60 && len < full + 8; len++) {
        LONG s = table(PSEUDO_SELF, len, &rl);
        ULONG want = len < full ? len & ~3UL : full;
        CHECKF(s == 0 && rl == want, "len %lu status 0x%lx rl 0x%lx want 0x%lx", len, (ULONG)s, rl, want);
        CHECKF(!memcmp(qb_big(), ref, want), "len %lu prefix differs", len);
        int clean = 1; for (ULONG i = want; i < want + 16; i++) if (qb_big()[i] != QB_FILL) clean = 0;
        CHECKF(clean, "len %lu wrote past the reported size", len);
    }
    EXPECT_EQ(table(PSEUDO_SELF, full + 4, &rl), 0); EXPECT_EQ(rl, full);
}
TEST(c58_rights_and_handle_errors) {
    REQUIRE(qb_have());
    struct { const char *n; HANDLE h; LONG want; } t[] = {
        { "lim", qb_child_open(R_LIM), ST_ACCESS }, { "qry", qb_child_open(R_QRY), ST_ACCESS },
        { "dup", qb_child_open(R_DUP), ST_ACCESS }, { "sync", qb_child_open(R_SYNC), ST_ACCESS },
        { "qry+dup", qb_child_open(R_QRY | R_DUP), ST_OK }, { "all", qb_child_open(PROCESS_ALL_ACCESS), ST_OK },
        { "lim+dup", qb_child_open(R_LIM | R_DUP), ST_ACCESS },
        { "zero", NULL, ST_BADHANDLE }, { "closed", qb_closed_handle(), ST_BADHANDLE }, { "thread", PSEUDO_THREAD, ST_TYPEMIS },
        { "pseudo", PSEUDO_SELF, ST_OK },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        ULONG rl, expect = 0;
        if (t[i].want == ST_OK && t[i].h != PSEUDO_SELF) expect = handle_count(t[i].h);
        LONG s = table(t[i].h, 0x400, &rl);
        CHECKF(s == t[i].want, "%s: status 0x%lx want 0x%lx", t[i].n, (ULONG)s, (ULONG)t[i].want);
        if (t[i].want == ST_OK) CHECKF(rl >= 4 && rl % 4 == 0, "%s: rl 0x%lx", t[i].n, rl);
        else CHECKF(rl == QB_SENT, "%s: rl 0x%lx touched", t[i].n, rl);
        if (t[i].want == ST_OK && expect && expect <= 0x100) CHECKF(rl == 4 * expect, "%s: rl 0x%lx handles %lu", t[i].n, rl, expect);
    }
    /* the length is checked before the handle */
    ULONG rl;
    EXPECT_EQ(table(NULL, 3, &rl), ST_LENMIS);
    EXPECT_EQ(table(qb_child_open(R_LIM), 0, &rl), ST_LENMIS);
    EXPECT_EQ(table(qb_child_open(R_LIM), 4, &rl), ST_ACCESS);
}
TEST(c58_child_table_matches_count) {
    REQUIRE(qb_have());
    HANDLE c = qb_child_open(R_QRY | R_DUP);
    REQUIRE(c);
    ULONG rl;
    ULONG n = handle_count(c);
    EXPECT_EQ(table(c, QB_BIG_SIZE, &rl), 0);
    EXPECT_EQ(rl, 4 * n);
    const unsigned char *b = qb_big();
    ULONG prev = 0;
    for (ULONG i = 0; i < n; i++) { ULONG v = rd32(b, i * 4); CHECK(v > prev && (v & 3) == 0); prev = v; }
    CloseHandle(c);
}
TEST(c58_null_arguments_and_exited_process) {
    REQUIRE(qb_have());
    ULONG rl = QB_SENT;
    EXPECT_EQ(qb_q(PSEUDO_SELF, 58, NULL, 0, &rl), ST_LENMIS);
    EXPECT_EQ(rl, QB_SENT);
    EXPECT_EQ(qb_q(PSEUDO_SELF, 58, qb_big(), 0x40, NULL), 0);
    HANDLE d = qb_dead();
    REQUIRE(d);
    EXPECT_EQ(table(d, 0x400, &rl), ST_TERMING);
    EXPECT_EQ(rl, 0);                                             /* ReturnLength is zeroed for an exited process */
    EXPECT_EQ(table(d, 4, &rl), ST_TERMING);
    EXPECT_EQ(rl, 0);
    EXPECT_EQ(table(d, 2, &rl), ST_LENMIS);
    EXPECT_EQ(rl, QB_SENT);
}
