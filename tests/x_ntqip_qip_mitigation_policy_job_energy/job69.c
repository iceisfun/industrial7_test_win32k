/* NtQueryInformationProcess class 69 (job memory information, 0x28 bytes).
 *
 * A process outside any job answers STATUS_PROCESS_NOT_IN_JOB and leaves the buffer alone; a process in a job
 * (created by the test, with the child assigned to it) answers with 0x28 bytes whose first three quadwords are
 * current job memory and two peaks. The length must be exactly 0x28 and is judged before the job membership.
 * Query rights PROCESS_QUERY_LIMITED_INFORMATION suffice. The test process may itself already be in a job (the
 * harness that starts it can use one), so the "not in a job" cases are conditional on the real membership.
 */
#include "qh.h"

struct jmem { ULONGLONG q[5]; };

static ULONG q69(HANDLE h, struct jmem *m, ULONG *rl) {
    static unsigned char b[0x30] __attribute__((aligned(8)));
    memset(b, 0xEE, sizeof b);
    ULONG st = qh(h, 69, b, 0x28, rl);
    CHECKF(b[0x28] == 0xEE, "byte past the buffer touched");
    memcpy(m, b, 0x28);
    return st;
}
static void check_success(const char *tag, const struct jmem *m, ULONG rl) {
    CHECKF(rl == 0x28, "%s: ReturnLength 0x%lx", tag, rl);
    CHECKF(m->q[0] != 0, "%s: current job memory is zero", tag);
    CHECKF(m->q[0] <= m->q[1] && m->q[1] <= m->q[2], "%s: counters not ordered cur<=peakA<=peakB", tag);
    CHECKF(m->q[0] != 0xEEEEEEEEEEEEEEEEULL && m->q[4] != 0xEEEEEEEEEEEEEEEEULL, "%s: buffer not fully written", tag);
}

TEST(j69_self_not_in_job) {
    if (qh_in_job(GetCurrentProcess()) != 0) { t_skip("the test process is itself in a job"); return; }
    unsigned char in[1] = {0};
    qh_err("pseudo", SELF, 69, 0x28, ST_NOT_IN_JOB, in, 0, 0);
    HANDLE lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    if (lim) { qh_err("lim", lim, 69, 0x28, ST_NOT_IN_JOB, in, 0, 0); CloseHandle(lim); }
}

TEST(j69_length_table) {
    static const ULONG lens[] = {0, 1, 4, 8, 0x10, 0x18, 0x20, 0x27, 0x29, 0x30, 0x38, 0x40, 0x100, 0x1000};
    for (int i = 0; i < sizeof lens / sizeof *lens; i++) qh_err("self", SELF, 69, lens[i], ST_LEN, NULL, 0, 0);
    struct qh_child c; HANDLE job = CreateJobObjectA(NULL, NULL);
    if (job && qh_child_start(&c, QC_PLAIN, job)) {
        HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
        if (h) { for (int i = 0; i < sizeof lens / sizeof *lens; i++) qh_err("job member", h, 69, lens[i], ST_LEN, NULL, 0, 0); CloseHandle(h); }
        qh_child_stop(&c);
    }
    if (job) CloseHandle(job);
}

TEST(j69_length_checked_before_membership) {
    /* wrong length wins over "not in a job" and over an invalid handle */
    qh_err("self len 0x20", SELF, 69, 0x20, ST_LEN, NULL, 0, 0);
    qh_err("null handle len 0x20", NULL, 69, 0x20, ST_LEN, NULL, 0, 0);
    qh_err("thread pseudo len 0x30", THREAD_PSEUDO, 69, 0x30, ST_LEN, NULL, 0, 0);
}

TEST(j69_child_in_job_succeeds) {
    struct qh_child c; HANDLE job = CreateJobObjectA(NULL, NULL);
    REQUIRE(job);
    if (!qh_child_start(&c, QC_PLAIN, job)) { t_skip("cannot start child in job"); CloseHandle(job); return; }
    HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
    REQUIRE(h);
    struct jmem m; ULONG rl; ULONG st = q69(h, &m, &rl);
    CHECKF(st == ST_OK, "status 0x%lx", st);
    if (st == ST_OK) check_success("lim", &m, rl);
    CloseHandle(h);
    h = qh_open(PROCESS_QUERY_INFORMATION, c.pid);
    if (h) { st = q69(h, &m, &rl); CHECKF(st == ST_OK, "qry status 0x%lx", st); if (st == ST_OK) check_success("qry", &m, rl); CloseHandle(h); }
    qh_child_stop(&c); CloseHandle(job);
}

TEST(j69_child_without_job_is_refused) {
    struct qh_child c;
    if (!qh_child_start(&c, QC_PLAIN, NULL)) { t_skip("cannot start child"); return; }
    if (qh_in_job(c.h) != 0) { t_skip("the child inherits a job from the harness"); qh_child_stop(&c); return; }
    HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
    REQUIRE(h);
    qh_err("child lim", h, 69, 0x28, ST_NOT_IN_JOB, NULL, 0, 0);
    CloseHandle(h);
    qh_child_stop(&c);
}

TEST(j69_rights_on_job_member) {
    struct qh_child c; HANDLE job = CreateJobObjectA(NULL, NULL);
    REQUIRE(job);
    if (!qh_child_start(&c, QC_PLAIN, job)) { t_skip("cannot start child in job"); CloseHandle(job); return; }
    static const struct { DWORD r; ULONG want; const char *n; } T[] = {
        {PROCESS_QUERY_LIMITED_INFORMATION, ST_OK, "lim"}, {PROCESS_QUERY_INFORMATION, ST_OK, "qry"},
        {PROCESS_VM_READ, ST_DENIED, "vmr"}, {PROCESS_TERMINATE, ST_DENIED, "term"}, {SYNCHRONIZE, ST_DENIED, "sync"}, {PROCESS_VM_WRITE, ST_DENIED, "vmw"},
    };
    for (int i = 0; i < sizeof T / sizeof *T; i++) {
        HANDLE h = qh_open(T[i].r, c.pid);
        if (!h) { t_note("open %s refused", T[i].n); continue; }
        if (T[i].want == ST_OK) { struct jmem m; ULONG rl; ULONG st = q69(h, &m, &rl); CHECKF(st == ST_OK, "%s: 0x%lx", T[i].n, st); }
        else qh_err(T[i].n, h, 69, 0x28, T[i].want, NULL, 0, 0);
        CloseHandle(h);
    }
    qh_child_stop(&c); CloseHandle(job);
}

TEST(j69_with_memory_limits_set) {
    struct qh_child c; HANDLE job = CreateJobObjectA(NULL, NULL);
    REQUIRE(job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION el; memset(&el, 0, sizeof el);
    el.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_JOB_MEMORY | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    el.JobMemoryLimit = 0x20000000; el.ProcessMemoryLimit = 0x10000000;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &el, sizeof el)) { t_skip("cannot set job limits"); CloseHandle(job); return; }
    if (!qh_child_start(&c, QC_PLAIN, job)) { t_skip("cannot start child in job"); CloseHandle(job); return; }
    HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
    REQUIRE(h);
    struct jmem m; ULONG rl; ULONG st = q69(h, &m, &rl);
    CHECKF(st == ST_OK, "status 0x%lx", st);
    if (st == ST_OK) {
        check_success("limited", &m, rl);
        CHECKF(m.q[0] <= el.JobMemoryLimit, "job memory 0x%llx above its limit", m.q[0]);
        CHECKF(m.q[2] <= el.JobMemoryLimit, "job peak 0x%llx above its limit", m.q[2]);
    }
    CloseHandle(h); qh_child_stop(&c); CloseHandle(job);
}

TEST(j69_two_members_report_the_same_job) {
    struct qh_child c1, c2; HANDLE job = CreateJobObjectA(NULL, NULL);
    REQUIRE(job);
    if (!qh_child_start(&c1, QC_PLAIN, job)) { t_skip("child 1"); CloseHandle(job); return; }
    if (!qh_child_start(&c2, QC_PLAIN, job)) { t_skip("child 2"); qh_child_stop(&c1); CloseHandle(job); return; }
    HANDLE h1 = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c1.pid), h2 = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c2.pid);
    REQUIRE(h1 && h2);
    struct jmem a, b; ULONG r1, r2;
    ULONG s1 = q69(h1, &a, &r1), s2 = q69(h2, &b, &r2);
    CHECKF(s1 == ST_OK && s2 == ST_OK, "0x%lx 0x%lx", s1, s2);
    if (s1 == ST_OK && s2 == ST_OK) {
        /* the job-wide counters are the same for every member (the processes are idle, so nothing moves) */
        int same = a.q[0] == b.q[0] && a.q[1] == b.q[1] && a.q[2] == b.q[2];
        if (!same) t_note("job counters of two idle members differ");
        CHECKF(a.q[0] == b.q[0], "job memory differs between members: 0x%llx 0x%llx", a.q[0], b.q[0]);
    }
    CloseHandle(h1); CloseHandle(h2); qh_child_stop(&c1); qh_child_stop(&c2); CloseHandle(job);
}

TEST(j69_terminated_member_still_answers) {
    struct qh_child c; HANDLE job = CreateJobObjectA(NULL, NULL);
    REQUIRE(job);
    if (!qh_child_start(&c, QC_PLAIN, job)) { t_skip("cannot start child in job"); CloseHandle(job); return; }
    HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
    REQUIRE(h);
    TerminateProcess(c.h, 0); WaitForSingleObject(c.h, 5000);
    struct jmem m; ULONG rl; ULONG st = q69(h, &m, &rl);
    CHECKF(st == ST_OK, "status 0x%lx", st);
    CloseHandle(h); qh_child_stop(&c); CloseHandle(job);
}

TEST(j69_handle_errors_with_exact_length) {
    HANDLE closed = NULL, ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    closed = qh_closed_handle();   /* last: nothing may be opened after it, or the value would be reused */
    qh_err("null", NULL, 69, 0x28, ST_INV_HANDLE, NULL, 0, 0);
    if (closed) qh_err("closed", closed, 69, 0x28, ST_INV_HANDLE, NULL, 0, 0);
    qh_err("thread pseudo", THREAD_PSEUDO, 69, 0x28, ST_TYPE_MISMATCH, NULL, 0, 0);
    qh_err("event", ev, 69, 0x28, ST_TYPE_MISMATCH, NULL, 0, 0);
    CloseHandle(ev);
}

TEST(j69_misaligned_buffer) {
    static unsigned char raw[0x60] __attribute__((aligned(16)));
    for (int off = 1; off < 4; off++) {
        CHECKF(qh(SELF, 69, raw + 16 + off, 0x28, NULL) == ST_MISALIGN, "off %d", off);
        CHECKF(qh(SELF, 69, raw + 16 + off, 0x20, NULL) == ST_MISALIGN, "off %d wrong len", off);
    }
}
